#pragma once
#include <Arduino.h>

bool event_log_begin();  // mounts LittleFS

void log_song(const String& uri, const String& title, uint32_t durationMs);
void log_clip_start(const String& filename, uint32_t songMs);
void log_clip_end(const String& filename, uint32_t songMs);
void clear_events();

String read_events();