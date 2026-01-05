#pragma once
#include <stdint.h>

struct SongMeta {
  char uri[192];
  char title[96];
  uint32_t durationMs;
};

extern SongMeta g_song;
extern uint32_t g_songTimeMs;