#pragma once

#include <SDL3/SDL.h>

enum { GRID_COLUMNS = 5, GRID_ROWS = 3 };

typedef struct {
  const char *title, *artist, *map;
} Song;

typedef struct {
  const char *title, *disc;
  const Song *songs;
  int count;
} Edition;

extern const Song songs[];
extern const Edition editions[];
extern const int edition_count;
typedef struct SongGrid SongGrid;

SongGrid *song_grid_create(SDL_Renderer *renderer);
void song_grid_destroy(SongGrid *grid);
void song_grid_preview(SongGrid *grid, int edition, int selected);
void song_grid_paint(SongGrid *grid, int edition, int selected, int first_row,
                     bool edition_focused, int loading, const char *status, int keyboard);
