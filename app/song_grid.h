#pragma once

#include <SDL3/SDL.h>

#include "session.h"

enum { GRID_COLUMNS = 5, GRID_ROWS = 3 };

typedef struct {
  const char *title, *artist, *map;
  const char *disc, *pack;
  unsigned content_index;
  float preview_gain, gameplay_gain;
  unsigned coach_count;
} Song;

typedef struct {
  const char *title, *disc;
  const Song *songs;
  int count;
} Edition;

typedef struct {
  uint8_t cursor[REMOTE_PLAYERS];
  unsigned present, locked;
} CoachPicker;

extern const Edition editions[];
extern const int edition_count;
const Edition *edition_for_disc(const char *disc);
typedef struct SongGrid SongGrid;

SongGrid *song_grid_create(SDL_Renderer *renderer);
void song_grid_destroy(SongGrid *grid);
void song_grid_preview(SongGrid *grid, int edition, int selected);
void song_grid_paint(SongGrid *grid, int edition, int selected, int first_row,
                     bool edition_focused, int loading, const char *status, int keyboard,
                     const CoachPicker *coaches);
