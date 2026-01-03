#include "event_log.h"
#include <LittleFS.h>

static const char* EVENTS_PATH = "/events.log";

bool event_log_begin() {
  // formatOnFail = true is handy early on
  return LittleFS.begin(true);
}

static void append_line(const String& line) {
  File f = LittleFS.open(EVENTS_PATH, "a");
  if (!f) return;
  f.println(line);
  f.close();
}

void log_song(const String& uri, const String& title, uint32_t durationMs) {
  append_line("SONG uri=\"" + uri + "\" title=\"" + title + "\" durationMs=" + String(durationMs));
}

void log_clip_start(const String& filename, uint32_t songMs) {
  append_line("CLIP_START file=\"" + filename + "\" songMs=" + String(songMs));
}

void log_clip_end(const String& filename, uint32_t songMs) {
  append_line("CLIP_END file=\"" + filename + "\" songMs=" + String(songMs));
}

void clear_events() {
  LittleFS.remove("/events.log");
}

String read_events() {
  File f = LittleFS.open(EVENTS_PATH, "r");
  if (!f) return "";
  String out;
  while (f.available()) out += char(f.read());
  f.close();
  return out;
}