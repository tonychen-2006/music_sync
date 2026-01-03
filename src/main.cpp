#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>   // for strcasecmp
#include <stdlib.h>
#include <LittleFS.h>

#include "app_state.h"

/*
  =========================
  EXTERNAL HOOKS
  =========================
  These must exist elsewhere in your project.
  If you get linker errors, your function names differ.
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
  METADATA PARSER
  =========================
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
  COMMAND PARSER
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
        Serial.println("CLIP START logged.");
      }
      break;
    }

    case 'b':
      log_clip_end(g_currentClipFilename, g_songTimeMs);
      Serial.println("CLIP END logged.");
      break;

    case 'r':
      Serial.println(read_events());
      break;

    case 'x':
      export_xml_from_events(read_events());
      Serial.println(read_project_xml());
      break;

    case 'c':
      clear_events();
      Serial.println("events.log cleared.");
      break;

    case 'm':     // muri=...;title=...;dur=...
      parse_and_set_metadata(line + 1);
      break;

    default:
      Serial.print("Unknown command: ");
      Serial.println(line);
      break;
  }
}

/*
  =========================
  BLE WRITE ENTRY POINT
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
  ARDUINO ENTRY POINTS
  =========================
*/
void setup() {
  Serial.begin(115200);
  delay(200);

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

  Serial.println("\n--- ready ---");
}

void loop() {
  serial_poll();
  delay(1);   // yield
}