#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>

#include "app_state.h"
#include "go_pro.h"

/*
  EXTERNAL HOOKS
*/
extern void log_song(const String& uri, const String& title, uint32_t durationMs);
extern void log_clip_start(const String& filename, uint32_t songMs);
extern void log_clip_end(const String& filename, uint32_t songMs);
extern void clear_events();
extern String read_events();
extern bool export_xml_from_events(const String& eventsText);
extern String read_project_xml();

/*
  GLOBALS
*/
String g_currentClipFilename = "";

/*
  BLE UUIDs (NUS-style)
*/
static const char* BLE_NAME = "MusicSync";
static const char* UUID_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* UUID_RX  = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"; // write
static const char* UUID_TX  = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"; // notify

static NimBLECharacteristic* g_ble_tx = nullptr;
static bool g_ble_subscribed = false;
static bool g_ble_send_xml_pending = false;

// Defer GoPro commands
static bool g_gopro_start_pending = false;
static bool g_gopro_stop_pending = false;

/*
  "Whole song" mode state (ADDED)
*/
static bool g_playing = false;          // set by p1/p0 from phone
static bool g_song_has_meta = false;    // set after metadata received
static bool g_song_recording = false;   // are we recording this song?
static String g_song_filename = "";     // filename used for clip start/end

/*
  UTILS
*/
/**
 * Safe string copy with null termination.
 * @param dst Destination buffer
 * @param dst_sz Size of destination buffer (including null terminator)
 * @param src Source string (can be NULL)
 * @brief Safely copies src to dst with bounds checking and null termination
 */
static void safe_copy(char* dst, size_t dst_sz, const char* src) {
  if (!dst || dst_sz == 0) return;
  if (!src) { dst[0] = '\0'; return; }
  strncpy(dst, src, dst_sz - 1);
  dst[dst_sz - 1] = '\0';
}

/**
 * Trim leading and trailing whitespace from a string in-place.
 * @param s String to trim (modifies in place)
 * @brief Removes all leading and trailing whitespace characters
 */
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

/**
 * Parse unsigned 32-bit integer from string.
 * @param s Input string (can be NULL)
 * @return Parsed value, or 0 if string is NULL or invalid
 * @brief Converts decimal string to uint32_t
 */
static uint32_t parse_u32(const char* s) {
  if (!s) return 0;
  return (uint32_t)strtoul(s, nullptr, 10);
}

/**
 * Check if all bytes in a buffer are ASCII digits.
 * @param d Data buffer
 * @param n Number of bytes to check
 * @return true if all bytes are '0'-'9', false otherwise
 * @brief Used to distinguish time commands (digits) from text commands
 */
static bool is_all_digits(const uint8_t* d, size_t n) {
  if (!d || n == 0) return false;
  for (size_t i = 0; i < n; i++)
    if (d[i] < '0' || d[i] > '9') return false;
  return true;
}

/**
 * Check if all bytes are printable ASCII (space-tilde, plus tab/CR/LF).
 * @param d Data buffer
 * @param n Number of bytes to check
 * @return true if all bytes are printable ASCII, false if contains control chars
 * @brief Validates BLE data before parsing as text commands
 */
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
  BLE TX HELPERS
*/
/**
 * Send XML data via BLE in chunks to avoid overwhelming the BLE stack.
 * @param xml Complete XML string to send
 * @brief Splits XML into 140-byte chunks with sequence numbers and sends via BLE notify.
 *        Sends begin marker, chunks, and end marker for reassembly on client.
 */
static void ble_send_xml_chunks(const String& xml) {
  if (!g_ble_tx || !g_ble_subscribed) return;

  const int CHUNK = 140;
  const int total = xml.length();
  int seq = 0;

  static char buf[200];

  snprintf(buf, sizeof(buf), "XML_BEGIN %d", total);
  g_ble_tx->setValue((uint8_t*)buf, strlen(buf));
  g_ble_tx->notify();
  delay(20);

  for (int i = 0; i < total; i += CHUNK) {
    int len = min(CHUNK, total - i);
    int headerLen = snprintf(buf, sizeof(buf), "XML_CHUNK %d ", seq);

    if (headerLen + len < (int)sizeof(buf)) {
      memcpy(buf + headerLen, xml.c_str() + i, len);
      g_ble_tx->setValue((uint8_t*)buf, headerLen + len);
      g_ble_tx->notify();
    }

    seq++;
    delay(20);
  }

  snprintf(buf, sizeof(buf), "XML_END %d", seq);
  g_ble_tx->setValue((uint8_t*)buf, strlen(buf));
  g_ble_tx->notify();
}

/*
  METADATA PARSER
  Format sent from iPhone: "muri=...;title=...;dur=..."
*/
/**
 * Parse and store song metadata from BLE/Serial input.
 * @param payload Format: "uri=<uri>;title=<title>;dur=<milliseconds>"
 * @brief Extracts Spotify URI, song title, and duration.
 *        Logs metadata and resets recording state for new song.
 */
static void parse_and_set_metadata(char* payload) {
  trim_inplace(payload);
  if (*payload == '\0') {
    Serial.println("metadata: empty");
    return;
  }

  // Reset song state on new metadata
  g_song_has_meta = false;

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

  log_song(String(g_song.uri), String(g_song.title), g_song.durationMs);

  // New song => stop old recording state (if any)
  if (g_song_recording) {
    g_song_recording = false;
    g_gopro_stop_pending = true;
    log_clip_end(g_song_filename, g_songTimeMs);
  }

  // Prepare filename for this song session
  g_song_filename = "song_" + String(millis()) + ".mp4";
  g_song_has_meta = true;
}

/*
  COMMAND PARSER
*/
/**
 * Parse and execute commands received from Serial/BLE.
 * @param line Command line to execute
 * @brief Processes commands:
 *        - m<metadata>: Set song metadata
 *        - p0/p1: Playback pause/play
 *        - x: Export events to XML
 *        - r: Read event log
 *        - c: Clear event log
 */
static void handle_command_line(char* line) {
  trim_inplace(line);
  if (*line == '\0') return;

  switch (line[0]) {

    case 'm': // metadata
      parse_and_set_metadata(line + 1);
      break;

    case 'p': { // playback state: p1 / p0 (ADDED)
      if (line[1] == '1') {
        g_playing = true;
        Serial.println("[PLAYBACK] PLAY");
      } else {
        g_playing = false;
        Serial.println("[PLAYBACK] PAUSE/STOP");
      }
      break;
    }

    case 'x': { // export xml
      export_xml_from_events(read_events());
      if (g_ble_subscribed) {
        g_ble_send_xml_pending = true;
        Serial.println("[BLE] XML export queued");
      } else {
        Serial.println(read_project_xml());
      }
      break;
    }

    case 'r':
      Serial.println(read_events());
      break;

    case 'c':
      clear_events();
      Serial.println("events.log cleared.");
      break;

    default:
      Serial.print("Unknown command: ");
      Serial.println(line);
      break;
  }
}

/*
  BLE WRITE ENTRY POINT
*/
/**
 * Handle incoming BLE write events from NUS RX characteristic.
 * @param data Received data bytes
 * @param len Number of bytes received
 * @brief Routes digit-only data to songTimeMs update, other data to command parser.
 *        Called in BLE task context; commands are deferred to main loop.
 */
void handle_ble_write(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;

  // Digits-only => song time ms
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
  BLE CALLBACKS
*/
/**
 * BLE TX characteristic callbacks.
 * @brief Tracks BLE notify subscription state to avoid sending when unsubscribed.
 */
class TxCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic* chr, ble_gap_conn_desc* desc, uint16_t subValue) override {
    (void)chr; (void)desc;
    g_ble_subscribed = (subValue & 0x0001) != 0;
    Serial.printf("[BLE] notify subscribed=%d\n", g_ble_subscribed ? 1 : 0);
  }
};

/**
 * BLE RX characteristic callbacks.
 * @brief Routes incoming BLE writes to command handler.
 */
class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c) override {
    std::string v = c->getValue();
    if (v.empty()) return;
    handle_ble_write((const uint8_t*)v.data(), v.size());
  }
};

/*
  SERIAL LINE READER (optional)
*/
static char linebuf[256];
static size_t linepos = 0;

/**
 * Poll serial port for incoming commands and execute them.
 * @brief Reads characters from Serial, builds lines, and executes on \r or \n.
 *        Useful for debugging and testing without BLE.
 */
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
  WHOLE SONG SCHEDULER (ADDED)
*/
/**
 * Auto-record entire song when playback is active.
 * @brief Manages recording start/stop based on playback state and song timing.
 *        Starts recording at song start, stops at song end or if playback is paused.
 */
static void whole_song_tick() {
  if (!g_song_has_meta) return;

  // If paused/stopped, stop recording (if active)
  if (!g_playing && g_song_recording) {
    g_song_recording = false;
    g_gopro_stop_pending = true;
    log_clip_end(g_song_filename, g_songTimeMs);
    Serial.println("[SONG] -> GoPro STOP (playback stopped)");
    return;
  }

  // Start recording near the beginning once playback is playing
  if (g_playing && !g_song_recording) {
    // "start condition": we are playing and time is near start (or we just started)
    if (g_songTimeMs <= 1500) {
      g_song_recording = true;
      g_gopro_start_pending = true;
      log_clip_start(g_song_filename, g_songTimeMs);
      Serial.println("[SONG] -> GoPro START (song begin)");
    }
  }

  // Stop recording at song end
  if (g_song_recording && g_song.durationMs > 0) {
    if (g_songTimeMs + 200 >= g_song.durationMs) { // small margin
      g_song_recording = false;
      g_gopro_stop_pending = true;
      log_clip_end(g_song_filename, g_songTimeMs);
      Serial.println("[SONG] -> GoPro STOP (song end)");
    }
  }
}

/*
  ARDUINO ENTRY POINTS
*/
/**
 * Initialize hardware: Serial, GoPro WiFi, LittleFS, and BLE.
 * @brief Sets up all subsystems and starts BLE advertising.
 *        Called once on startup.
 */
void setup() {
  Serial.begin(115200);
  delay(200);

  bool ok = goproBegin("GP26354747", "scuba0828");
  Serial.println(ok ? "[GoPro] WiFi connected" : "[GoPro] WiFi connect FAILED");

  if (!LittleFS.begin(false)) {
    Serial.println("[FS] LittleFS mount failed. Formatting...");
    if (!LittleFS.begin(true)) {
      Serial.println("[FS] LittleFS format+mount failed. FILE IO DISABLED.");
    } else {
      Serial.println("[FS] LittleFS formatted and mounted.");
    }
  } else {
    Serial.println("[FS] LittleFS mounted.");
  }

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
  Serial.println("Commands from phone: digits(timeMs), muri/title/dur, p1/p0, x(export xml)");
}

/**
 * Main event loop: process serial/BLE commands and deferred operations.
 * @brief Continuously polls for commands and executes deferred GoPro/XML operations.
 *        Runs in main task context with sufficient stack for HTTP requests.
 */
void loop() {
  serial_poll();

  // Whole song auto record
  whole_song_tick();

  // GoPro start
  if (g_gopro_start_pending) {
    g_gopro_start_pending = false;
    bool ok = goproShutter(true);
    Serial.println(ok ? "[GoPro] rec START ok" : "[GoPro] rec START FAIL");
  }

  // GoPro stop
  if (g_gopro_stop_pending) {
    g_gopro_stop_pending = false;
    bool ok = goproShutter(false);
    Serial.println(ok ? "[GoPro] rec STOP ok" : "[GoPro] rec STOP FAIL");
  }

  // XML send
  if (g_ble_send_xml_pending) {
    g_ble_send_xml_pending = false;
    String xml = read_project_xml();
    Serial.println(xml); // optional
    ble_send_xml_chunks(xml);
    Serial.println("[BLE] XML sent");
  }

  delay(1);
}