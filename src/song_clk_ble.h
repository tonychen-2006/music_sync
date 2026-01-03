#pragma once
#include <Arduino.h>

// Starts BLE advertising and exposes a writable characteristic "song_time_ms".
void song_clock_begin();

// Update/read current song time (ms). BLE writes call set_time internally.
void song_clock_set_time(uint32_t ms);
uint32_t song_clock_get_time();