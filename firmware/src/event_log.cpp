#include "event_log.h"
#include <LittleFS.h>

static const char* EVENTS_PATH = "/events.log";

/**
 * Initialize LittleFS filesystem for event logging.
 * @return true if filesystem mounted successfully, false otherwise
 * @brief Mounts LittleFS with auto-format enabled for first-time setup.
 */
bool event_log_begin() {
  // formatOnFail = true is handy early on
  return LittleFS.begin(true);
}

/**
 * Append a line to the events.log file.
 * @param line Text line to append (newline will be added automatically)
 * @brief Opens file in append mode, writes line, closes file.
 *        Fails silently if file cannot be opened.
 */
static void append_line(const String& line) {
  File f = LittleFS.open(EVENTS_PATH, "a");
  if (!f) return;
  f.println(line);
  f.close();
}

/**
 * Log song metadata to events.log.
 * @param uri Song URI (e.g., Spotify track URI)
 * @param title Song title
 * @param durationMs Song duration in milliseconds
 * @brief Writes SONG event with URI, title, and duration for timeline export.
 */
void log_song(const String& uri, const String& title, uint32_t durationMs) {
  append_line("SONG uri=\"" + uri + "\" title=\"" + title + "\" durationMs=" + String(durationMs));
}

/**
 * Log clip recording start event.
 * @param filename Video filename (e.g., "GOPR0001.MP4")
 * @param songMs Song playback time in milliseconds when recording started
 * @brief Writes CLIP_START event for synchronizing video with audio timeline.
 */
void log_clip_start(const String& filename, uint32_t songMs) {
  append_line("CLIP_START file=\"" + filename + "\" songMs=" + String(songMs));
}

/**
 * Log clip recording end event.
 * @param filename Video filename (e.g., "GOPR0001.MP4")
 * @param songMs Song playback time in milliseconds when recording stopped
 * @brief Writes CLIP_END event for synchronizing video with audio timeline.
 */
void log_clip_end(const String& filename, uint32_t songMs) {
  append_line("CLIP_END file=\"" + filename + "\" songMs=" + String(songMs));
}

/**
 * Clear the events.log file.
 * @brief Deletes /events.log to start a fresh recording session.
 */
void clear_events() {
  LittleFS.remove("/events.log");
}

/**
 * Read entire events.log file contents.
 * @return Complete contents of events.log, or empty string if file doesn't exist
 * @brief Reads all logged events for processing/export to XML.
 */
String read_events() {
  File f = LittleFS.open(EVENTS_PATH, "r");
  if (!f) return "";
  String out;
  while (f.available()) out += char(f.read());
  f.close();
  return out;
}