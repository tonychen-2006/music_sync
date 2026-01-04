#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>   // for strcasecmp
#include <stdlib.h>
#include <LittleFS.h>

#include <NimBLEDevice.h>   // ADDED (BLE notify)

#include "app_state.h"
#include "go_pro.h"

/*
  =========================
  EXTERNAL HOOKS
  =========================
  These must exist elsewhere in your project.
*/
extern void log_song(const String& uri, const String& title, uint32_t durationMs);
extern void log_clip_start(const String& filename, uint32_t songMs);
extern void log_clip_end(const String& filename, uint32_t songMs);
extern void clear_events();
extern String read_events();
extern bool export_xml_from_events(const String& eventsText);
extern String read_project_xml();

/*
  =========================
  GLOBAL STATE
  =========================
*/
String g_currentClipFilename = "";

/*
  =========================
  BLE (ONLY ADDITION: TX notify + XML chunk send)
  =========================
  Uses NUS-style UUIDs so nRF Connect is easy.
*/
static const char* BLE_NAME = "MusicSync";
static const char* UUID_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* UUID_RX  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // write
static const char* UUID_TX  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // notify

static NimBLECharacteristic* g_ble_tx = nullptr;
static bool g_ble_subscribed = false;
static bool g_ble_send_xml_pending = false;  // Flag to defer XML sending to main loop

// Flags to defer GoPro commands to main loop (prevents BLE task watchdog timeout)
static bool g_gopro_start_pending = false;
static bool g_gopro_stop_pending = false;
static String g_pending_clip_filename = "";

/*
  =========================
  UTILS
  =========================
*/
static void safe_copy(char* dst, size_t dst_sz, const char* src) {
  if (!dst || dst_sz == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, dst_sz - 1);
  dst[dst_sz - 1] = '\0';
}

static void trim_inplace(char* s) {
  if (!s) return;
  char* p = s;
  while (*p && isspace((unsigned char)*p)) p++;
  if (p != s) memmove(s, p, strlen(p) + 1);
  size_t n = strlen(s);
  while (n > 0 && isspace((unsigned char)s[n - 1])) {
    s[n - 1] = '\0';
    n--;
  }
}

static uint32_t parse_u32(const char* s) {
  if (!s) return 0;
  return (uint32_t)strtoul(s, nullptr, 10);
}

static bool is_all_digits(const uint8_t* d, size_t n) {
  if (!d || n == 0) return false;
  for (size_t i = 0; i < n; i++)
    if (d[i] < '0' || d[i] > '9') return false;
  return true;
}

static bool is_printable_ascii(const uint8_t* d, size_t n) {
  if (!d || n == 0) return false;
  for (size_t i = 0; i < n; i++) {
    uint8_t c = d[i];
    if (c == '\r' || c == '\n' || c == '\t') continue;
    if (c < 32 || c > 126) return false;
  }
  return true;
}

/*
  =========================
  BLE TX HELPERS (ADDED)
  =========================
*/
static void ble_notify_text(const String& s) {
  if (!g_ble_tx || !g_ble_subscribed) return;
  g_ble_tx->setValue((uint8_t*)s.c_str(), s.length());
  g_ble_tx->notify();
}

static void ble_send_xml_chunks(const String& xml) {
  if (!g_ble_tx || !g_ble_subscribed) return;

  const int CHUNK = 140;  // Reduced to leave room for header
  const int total = xml.length();
  int seq = 0;

  // Use static buffer to avoid stack allocation
  static char buf[200];
  
  snprintf(buf, sizeof(buf), "XML_BEGIN %d", total);
  g_ble_tx->setValue((uint8_t*)buf, strlen(buf));
  g_ble_tx->notify();
  delay(20);

  for (int i = 0; i < total; i += CHUNK) {
    int len = min(CHUNK, total - i);
    int headerLen = snprintf(buf, sizeof(buf), "XML_CHUNK %d ", seq);
    
    // Copy chunk after header
    if (headerLen + len < (int)sizeof(buf)) {
      memcpy(buf + headerLen, xml.c_str() + i, len);
      g_ble_tx->setValue((uint8_t*)buf, headerLen + len);
      g_ble_tx->notify();
    }
    
    seq++;
    delay(20);  // Increased delay to prevent overwhelming BLE stack
  }

  snprintf(buf, sizeof(buf), "XML_END %d", seq);
  g_ble_tx->setValue((uint8_t*)buf, strlen(buf));
  g_ble_tx->notify();
}

/*
  =========================
  METADATA PARSER
  =========================
  Format: muri=...;title=...;dur=...
*/
static void parse_and_set_metadata(char* payload) {
  trim_inplace(payload);
  if (*payload == '\0') {
    Serial.println("metadata: empty");
    return;
  }

  char* save = nullptr;
  for (char* tok = strtok_r(payload, ";", &save);
       tok;
       tok = strtok_r(nullptr, ";", &save)) {

    trim_inplace(tok);
    char* eq = strchr(tok, '=');
    if (!eq) continue;

    *eq = '\0';
    char* key = tok;
    char* val = eq + 1;
    trim_inplace(key);
    trim_inplace(val);

    if (strcasecmp(key, "uri") == 0) {
      safe_copy(g_song.uri, sizeof(g_song.uri), val);
    } else if (strcasecmp(key, "title") == 0) {
      safe_copy(g_song.title, sizeof(g_song.title), val);
    } else if (strcasecmp(key, "dur") == 0 || strcasecmp(key, "duration") == 0) {
      g_song.durationMs = parse_u32(val);
    }
  }

  Serial.printf(
    "Song meta set: uri=\"%s\" title=\"%s\" durationMs=%u\n",
    g_song.uri, g_song.title, (unsigned)g_song.durationMs
  );

  // Log song to events.log
  log_song(String(g_song.uri), String(g_song.title), g_song.durationMs);
}

/*
  =========================
  COMMAND PARSER (UNCHANGED LOGIC)
  =========================
*/
static void handle_command_line(char* line) {
  trim_inplace(line);
  if (*line == '\0') return;

  switch (line[0]) {
    case 't': {   // t<number>
      char* p = line + 1;
      while (*p && isspace((unsigned char)*p)) p++;
      g_songTimeMs = parse_u32(p);
      Serial.printf("songTimeMs = %u\n", (unsigned)g_songTimeMs);
      break;
    }

    case 'a': {   // a<filename>
      char* fn = line + 1;
      while (*fn && isspace((unsigned char)*fn)) fn++;
      if (*fn) {
        g_currentClipFilename = String(fn);
        log_clip_start(g_currentClipFilename, g_songTimeMs);
        
        // Defer GoPro command to main loop to avoid blocking BLE task
        g_pending_clip_filename = g_currentClipFilename;
        g_gopro_start_pending = true;
      }
      break;
    }

    case 'b': {
      if (g_currentClipFilename.length() == 0) {
        Serial.println("clip end: no active clip");
        break;
      }

      log_clip_end(g_currentClipFilename, g_songTimeMs);
      
      // Defer GoPro command to main loop to avoid blocking BLE task
      g_gopro_stop_pending = true;
      g_currentClipFilename = "";
      break;
    }

    case 'r':
      Serial.println(read_events());
      break;

    case 'x': {
      export_xml_from_events(read_events());
      
      if (g_ble_subscribed) {
        // Defer sending to main loop to avoid BLE task stack overflow
        g_ble_send_xml_pending = true;
        Serial.println("[BLE] XML export queued");
      } else {
        // Only print to serial if not sending via BLE
        Serial.println(read_project_xml());
      }
      break;
    }

    case 'c':
      clear_events();
      Serial.println("events.log cleared.");
      break;

    case 'm':     // muri=...;title=...;dur=...
      parse_and_set_metadata(line + 1);
      break;

    case 'g': {
      // gs = GoPro start, ge = GoPro end
      if (line[1] == 's') {
        g_gopro_start_pending = true;
      } else if (line[1] == 'e') {
        g_gopro_stop_pending = true;
      } else {
        Serial.println("Use: gs (start) or ge (end)");
      }
      break;
    }

    default:
      Serial.print("Unknown command: ");
      Serial.println(line);
      break;
  }
}

/*
  =========================
  BLE WRITE ENTRY POINT (UNCHANGED)
  =========================
*/
void handle_ble_write(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  if (is_all_digits(data, len)) {
    char tmp[32];
    size_t n = (len < sizeof(tmp) - 1) ? len : sizeof(tmp) - 1;
    memcpy(tmp, data, n);
    tmp[n] = '\0';
    g_songTimeMs = parse_u32(tmp);
    Serial.printf("[BLE] songTimeMs = %u\n", (unsigned)g_songTimeMs);
    return;
  }

  if (is_printable_ascii(data, len)) {
    char buf[256];
    size_t n = (len < sizeof(buf) - 1) ? len : sizeof(buf) - 1;
    memcpy(buf, data, n);
    buf[n] = '\0';
    handle_command_line(buf);
  }
}

/*
  =========================
  BLE CALLBACKS (ADDED)
  =========================
*/
class TxCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* chr, ble_gap_conn_desc* desc, uint16_t subValue) override {
    (void)chr; (void)desc;
    g_ble_subscribed = (subValue & 0x0001) != 0; // notify
    Serial.printf("[BLE] notify subscribed=%d\n", g_ble_subscribed ? 1 : 0);
  }
};

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    if (v.empty()) return;
    handle_ble_write((const uint8_t*)v.data(), v.size());
  }
};

/*
  =========================
  SERIAL LINE READER
  =========================
*/
static char linebuf[256];
static size_t linepos = 0;

static void serial_poll() {
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (linepos) {
        linebuf[linepos] = '\0';
        handle_command_line(linebuf);
        linepos = 0;
      }
    } else if (linepos < sizeof(linebuf) - 1) {
      linebuf[linepos++] = ch;
    }
  }
}

/*
  =========================
  ARDUINO ENTRY POINTS (KEEP OLD BEHAVIOR)
  =========================
*/
void setup() {
  Serial.begin(115200);
  delay(200);

  // KEEP: GoPro connect behavior exactly as before
  bool ok = goproBegin("GP26354747", "scuba0828");
  Serial.println(ok ? "[GoPro] WiFi connected" : "[GoPro] WiFi connect FAILED");

  // KEEP: LittleFS mount behavior as before
  if (!LittleFS.begin(false)) {                 // false = don't auto-format
    Serial.println("[FS] LittleFS mount failed. Formatting...");
    if (!LittleFS.begin(true)) {                // true = format if mount fails
      Serial.println("[FS] LittleFS format+mount failed. FILE IO DISABLED.");
    } else {
      Serial.println("[FS] LittleFS formatted and mounted.");
    }
  } else {
    Serial.println("[FS] LittleFS mounted.");
  }

  // ADDED: BLE init (does not touch WiFi logic)
  NimBLEDevice::init(BLE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer* server = NimBLEDevice::createServer();
  NimBLEService* svc = server->createService(UUID_SVC);

  NimBLECharacteristic* rx = svc->createCharacteristic(
    UUID_RX,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  rx->setCallbacks(new RxCallbacks());

  g_ble_tx = svc->createCharacteristic(
    UUID_TX,
    NIMBLE_PROPERTY::NOTIFY
  );
  g_ble_tx->setCallbacks(new TxCallbacks());

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(UUID_SVC);
  adv->start();

  Serial.println("\n--- ready ---");
}

void loop() {
  serial_poll();
  
  // Handle pending GoPro start command
  if (g_gopro_start_pending) {
    g_gopro_start_pending = false;
    bool ok = goproShutter(true);
    Serial.println(ok ? "[GoPro] rec START ok" : "[GoPro] rec START FAIL");
  }
  
  // Handle pending GoPro stop command
  if (g_gopro_stop_pending) {
    g_gopro_stop_pending = false;
    bool ok = goproShutter(false);
    Serial.println(ok ? "[GoPro] rec STOP ok" : "[GoPro] rec STOP FAIL");
  }
  
  // Handle pending XML send in main loop context (more stack space)
  if (g_ble_send_xml_pending) {
    g_ble_send_xml_pending = false;
    String xml = read_project_xml();
    Serial.println(xml);  // Also print to serial
    ble_send_xml_chunks(xml);
    Serial.println("[BLE] XML sent");
  }
  
  delay(1);   // yield
}