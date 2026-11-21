#include "song_grid.h"
#include "paths.h"

#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <limits.h>

enum {
  WIDTH = 1440, HEIGHT = 810, FONT_COUNT = 3,
  FIRST_GLYPH = 32, GLYPH_COUNT = 224, ATLAS_COLUMNS = 16,
  PREVIEW_WIDTH = 160, PREVIEW_HEIGHT = 120, PREVIEW_FRAME_MS = 100,
  PREVIEW_PITCH = PREVIEW_WIDTH * 3, PREVIEW_FRAME_BYTES = PREVIEW_PITCH * PREVIEW_HEIGHT,
};
enum GridFont { FOOTER, TILE, HEADING };

typedef struct {
  SDL_FRect bounds;
  int left, top, advance;
} Glyph;

typedef struct {
  SDL_Texture *atlas;
  Glyph letters[GLYPH_COUNT];
} Font;

typedef struct {
  uint8_t *pcm;
  uint32_t bytes;
  SDL_AudioSpec format;
} Preview;

struct SongGrid {
  SDL_Renderer *renderer;
  SDL_Texture **covers[EDITION_COUNT];
  Font fonts[FONT_COUNT];
  Preview audio_preview;
  SDL_AudioStream *audio;
  SDL_Texture *video;
  uint8_t *frames;
  size_t frame_count, last_frame;
  const Song *preview;
  uint64_t preview_at, preview_started;
  char cache[PATH_MAX];
};

static const SDL_Color white = {248, 247, 255, 255};
static const SDL_Color muted = {190, 181, 216, 255};
static const SDL_Color accent[] = {{206, 255, 119, 255}, {255, 200, 105, 255}};

static void make_font(SDL_Renderer *renderer, FT_Face face, int size, Font *font) {
  if (!face || FT_Set_Pixel_Sizes(face, 0, size)) return;
  int cell = size + 8;
  SDL_Surface *pixels = SDL_CreateSurface(cell * ATLAS_COLUMNS, cell * ((GLYPH_COUNT + ATLAS_COLUMNS - 1) / ATLAS_COLUMNS),
                                          SDL_PIXELFORMAT_RGBA32);
  if (!pixels) return;
  SDL_ClearSurface(pixels, 0, 0, 0, 0);
  for (int i = 0; i < GLYPH_COUNT; i++) {
    if (FT_Load_Char(face, i + FIRST_GLYPH, FT_LOAD_RENDER)) continue;
    FT_GlyphSlot glyph = face->glyph;
    FT_Bitmap *bitmap = &glyph->bitmap;
    int x = i % ATLAS_COLUMNS * cell, y = i / ATLAS_COLUMNS * cell;
    font->letters[i] = (Glyph){
      .bounds = {x, y, bitmap->width, bitmap->rows},
      .left = glyph->bitmap_left, .top = glyph->bitmap_top, .advance = glyph->advance.x >> 6,
    };
    for (unsigned row = 0; row < bitmap->rows; row++) {
      uint8_t *pixel = (uint8_t *)pixels->pixels + (y + row) * pixels->pitch + x * 4;
      const uint8_t *alpha = bitmap->buffer + row * bitmap->pitch;
      for (unsigned column = 0; column < bitmap->width; column++, pixel += 4) {
        pixel[0] = pixel[1] = pixel[2] = 255;
        pixel[3] = alpha[column];
      }
    }
  }
  font->atlas = SDL_CreateTextureFromSurface(renderer, pixels);
  SDL_DestroySurface(pixels);
}

static void load_fonts(SongGrid *grid) {
  FT_Library freetype;
  if (FT_Init_FreeType(&freetype)) return;
  FcConfig *config = FcInitLoadConfigAndFonts();
  if (config) {
    for (int i = 0; i < FONT_COUNT; i++) {
      static const int size[] = {19, 24, 60};
      FcPattern *pattern = FcPatternBuild(NULL, FC_FAMILY, FcTypeString, "sans-serif",
                                         FC_WEIGHT, FcTypeInteger,
                                         i == FOOTER ? FC_WEIGHT_REGULAR : FC_WEIGHT_BOLD, NULL);
      if (!pattern) continue;
      FcConfigSubstitute(config, pattern, FcMatchPattern);
      FcDefaultSubstitute(pattern);
      FcResult result;
      FcPattern *match = FcFontMatch(config, pattern, &result);
      FcPatternDestroy(pattern);
      if (!match) continue;
      FcChar8 *path;
      FT_Face face;
      if (FcPatternGetString(match, FC_FILE, 0, &path) == FcResultMatch &&
          FT_New_Face(freetype, (const char *)path, 0, &face) == 0) {
        make_font(grid->renderer, face, size[i], &grid->fonts[i]);
        FT_Done_Face(face);
      }
      FcPatternDestroy(match);
    }
    FcConfigDestroy(config);
  }
  FT_Done_FreeType(freetype);
}

static int preview_path(char *path, const SongGrid *grid, const Edition *edition, const Song *song,
                        const char *extension) {
  if (!grid->cache[0] || !song->map) return 0;
  char key[32];
  SDL_strlcpy(key, song->map, sizeof key);
  SDL_strlwr(key);
  int length = snprintf(path, PATH_MAX, "%s/previews/%s/%s.%s", grid->cache, edition->disc, key, extension);
  return length > 0 && length < PATH_MAX ? length : 0;
}

static SDL_Texture *coach_thumbnail(SongGrid *grid, const Edition *edition, const Song *song) {
  char path[PATH_MAX];
  if (!preview_path(path, grid, edition, song, "rgb")) return NULL;
  SDL_IOStream *file = SDL_IOFromFile(path, "rb");
  if (!file) return NULL;
  uint8_t frame[PREVIEW_FRAME_BYTES];
  Sint64 size = SDL_GetIOSize(file);
  // Read one frame from the middle of the cached clip without loading the video.
  Sint64 middle = (size / PREVIEW_FRAME_BYTES / 2) * PREVIEW_FRAME_BYTES;
  bool read = size >= PREVIEW_FRAME_BYTES && size % PREVIEW_FRAME_BYTES == 0 &&
              SDL_SeekIO(file, middle, SDL_IO_SEEK_SET) == middle &&
              SDL_ReadIO(file, frame, sizeof frame) == sizeof frame;
  SDL_CloseIO(file);
  if (!read) return NULL;
  SDL_Texture *texture = SDL_CreateTexture(grid->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC,
                                           PREVIEW_WIDTH, PREVIEW_HEIGHT);
  if (texture && !SDL_UpdateTexture(texture, NULL, frame, PREVIEW_PITCH)) {
    SDL_DestroyTexture(texture);
    return NULL;
  }
  return texture;
}

SongGrid *song_grid_create(SDL_Renderer *renderer) {
  SongGrid *grid = calloc(1, sizeof *grid);
  if (!grid) return NULL;
  grid->renderer = renderer;
  load_fonts(grid);
  SDL_InitSubSystem(SDL_INIT_AUDIO);
  if (!root_path(grid->cache, sizeof grid->cache, "XDG_CACHE_HOME", ".cache")) grid->cache[0] = 0;
  for (int edition = 0; edition < EDITION_COUNT; edition++) {
    const Edition *page = &editions[edition];
    grid->covers[edition] = calloc(page->count, sizeof *grid->covers[edition]);
    if (!grid->covers[edition]) {
      song_grid_destroy(grid);
      return NULL;
    }
    for (int i = 0; i < page->count; i++)
      grid->covers[edition][i] = coach_thumbnail(grid, page, &page->songs[i]);
  }
  SDL_SetRenderLogicalPresentation(renderer, WIDTH, HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  return grid;
}

static void stop_preview(SongGrid *grid) {
  SDL_DestroyAudioStream(grid->audio);
  SDL_DestroyTexture(grid->video);
  SDL_free(grid->audio_preview.pcm);
  SDL_free(grid->frames);
  grid->audio = NULL;
  grid->video = NULL;
  grid->audio_preview = (Preview){0};
  grid->frames = NULL;
  grid->frame_count = 0;
  grid->preview_started = 0;
}

void song_grid_destroy(SongGrid *grid) {
  stop_preview(grid);
  for (int edition = 0; edition < EDITION_COUNT; edition++) {
    if (!grid->covers[edition]) continue;
    for (int i = 0; i < editions[edition].count; i++) SDL_DestroyTexture(grid->covers[edition][i]);
    free(grid->covers[edition]);
  }
  for (int i = 0; i < FONT_COUNT; i++) SDL_DestroyTexture(grid->fonts[i].atlas);
  free(grid);
}

static void open_preview(SongGrid *grid, const Edition *edition, const Song *song, uint64_t now) {
  char path[PATH_MAX];
  int length = preview_path(path, grid, edition, song, "wav");
  grid->preview_started = now;
  if (!length) return;

  Preview *audio = &grid->audio_preview;
  if (SDL_LoadWAV(path, &audio->format, &audio->pcm, &audio->bytes)) {
    grid->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audio->format, NULL, NULL);
    if (grid->audio) {
      SDL_SetAudioStreamGain(grid->audio, 0.55f);
      SDL_PutAudioStreamData(grid->audio, audio->pcm, (int)audio->bytes);
      SDL_ResumeAudioStreamDevice(grid->audio);
    }
  }
  memcpy(path + length - 3, "rgb", 3);
  size_t size;
  grid->frames = SDL_LoadFile(path, &size);
  if (grid->frames && size >= PREVIEW_FRAME_BYTES && size % PREVIEW_FRAME_BYTES == 0) {
    grid->frame_count = size / PREVIEW_FRAME_BYTES;
    grid->last_frame = SIZE_MAX;
    grid->video = SDL_CreateTexture(grid->renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                    PREVIEW_WIDTH, PREVIEW_HEIGHT);
  }
}

void song_grid_preview(SongGrid *grid, int edition, int selected) {
  uint64_t now = SDL_GetTicks();
  const Edition *page = &editions[edition];
  const Song *song = selected < 0 ? NULL : &page->songs[selected];
  if (grid->preview != song) {
    stop_preview(grid);
    grid->preview = song;
    grid->preview_at = now + 225;
  }
  if (!song || !song->map || now < grid->preview_at) return;
  if (!grid->preview_started) open_preview(grid, page, song, now);
  if (grid->audio && SDL_GetAudioStreamQueued(grid->audio) < grid->audio_preview.format.freq / 2)
    SDL_PutAudioStreamData(grid->audio, grid->audio_preview.pcm, (int)grid->audio_preview.bytes);
  if (grid->video) {
    size_t frame = (now - grid->preview_started) / PREVIEW_FRAME_MS % grid->frame_count;
    if (frame != grid->last_frame) {
      grid->last_frame = frame;
      SDL_UpdateTexture(grid->video, NULL, grid->frames + frame * PREVIEW_FRAME_BYTES, PREVIEW_PITCH);
    }
  }
}

static void rectangle(SDL_Renderer *renderer, SDL_FRect rect, SDL_Color color) {
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  SDL_RenderFillRect(renderer, &rect);
}

static int glyph_index(uint32_t code) {
  if (code < FIRST_GLYPH || code >= FIRST_GLYPH + GLYPH_COUNT) code = '?';
  return code - FIRST_GLYPH;
}

static void text(SongGrid *grid, enum GridFont style, const char *label, float x, float line, SDL_Color color) {
  Font *font = &grid->fonts[style];
  SDL_Renderer *renderer = grid->renderer;
  if (!font->atlas) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDebugText(renderer, x, line - 8, label);
    return;
  }
  SDL_SetTextureColorMod(font->atlas, color.r, color.g, color.b);
  while (*label) {
    const Glyph *letter = &font->letters[glyph_index(SDL_StepUTF8(&label, NULL))];
    if (letter->bounds.w && letter->bounds.h) {
      SDL_FRect target = {x + letter->left, line - letter->top, letter->bounds.w, letter->bounds.h};
      SDL_RenderTexture(renderer, font->atlas, &letter->bounds, &target);
    }
    x += letter->advance;
  }
}

static void text_fit(SongGrid *grid, enum GridFont style, const char *label,
                     float x, float line, float width, SDL_Color color) {
  const Font *font = &grid->fonts[style];
  const int ellipsis = 3 * (font->atlas ? font->letters['.' - FIRST_GLYPH].advance : 8);
  char fitted[128];
  const char *end = label;
  float remaining = width;
  while (*end) {
    const char *next = end;
    int i = glyph_index(SDL_StepUTF8(&next, NULL));
    remaining -= font->atlas ? font->letters[i].advance : 8;
    if (remaining < (*next ? ellipsis : 0) || next - label >= (int)sizeof fitted - 3) break;
    end = next;
  }
  if (*end) {
    snprintf(fitted, sizeof fitted, "%.*s...", (int)(end - label), label);
    label = fitted;
  }
  text(grid, style, label, x, line, color);
}

static void background(SDL_Renderer *renderer) {
  SDL_SetRenderDrawColor(renderer, 18, 12, 35, 255);
  SDL_RenderClear(renderer);
  const SDL_Vertex gradient[] = {
    {{0, 0}, {.31f, .14f, .43f, 1}, {0, 0}}, {{WIDTH, 0}, {.17f, .09f, .31f, 1}, {0, 0}},
    {{WIDTH, HEIGHT}, {.06f, .035f, .12f, 1}, {0, 0}}, {{0, HEIGHT}, {.14f, .07f, .24f, 1}, {0, 0}},
  };
  const int faces[] = {0, 1, 2, 0, 2, 3};
  SDL_RenderGeometry(renderer, NULL, gradient, 4, faces, 6);
  SDL_SetRenderDrawColor(renderer, 154, 94, 201, 42);
  for (int i = 0; i < 6; i++)
    SDL_RenderLine(renderer, 1070 + i * 45, 0, 1440, 255 + i * 42);
}

void song_grid_paint(SongGrid *grid, int edition, int selected, int first_row,
                     int loading, const char *status, int keyboard) {
  SDL_Renderer *renderer = grid->renderer;
  const Edition *page = &editions[edition];
  const Song *choice = &page->songs[selected];
  SDL_Color highlight = accent[edition];
  background(renderer);
  rectangle(renderer, (SDL_FRect){68, 60, 8, 50}, highlight);
  text(grid, HEADING, page->title, 96, 108, white);

  if (!loading) {
    const char *neighbor = edition > 0 ? "< Just Dance 2014" : "Just Dance 2015 >";
    const char *direction = edition > 0 ? "Left at row start" : "Right at row end";
    text(grid, TILE, neighbor, 1120, 76, white);
    text(grid, FOOTER, direction, 1120, 103, muted);
  }
  for (int i = 0; i < GRID_COLUMNS * GRID_ROWS; i++) {
    SDL_FRect slot = {68 + (i % GRID_COLUMNS) * 264, 156 + (i / GRID_COLUMNS) * 190, 244, 174};
    int entry = first_row * GRID_COLUMNS + i;
    if (entry == selected)
      rectangle(renderer, (SDL_FRect){slot.x - 4, slot.y - 4, slot.w + 8, slot.h + 8}, highlight);
    rectangle(renderer, slot, entry < page->count ? (SDL_Color){32, 22, 53, 255} : (SDL_Color){35, 22, 58, 143});
    if (entry >= page->count) {
      SDL_SetRenderDrawColor(renderer, 210, 156, 243, 28);
      SDL_RenderRect(renderer, &slot);
      continue;
    }
    const Song *song = &page->songs[entry];
    SDL_Texture *thumbnail = grid->covers[edition][entry];
    int video = grid->video && grid->preview == song;
    if (video || thumbnail) {
      float width = 115.0f * PREVIEW_WIDTH / PREVIEW_HEIGHT;
      SDL_FRect screen = {slot.x + (slot.w - width) / 2, slot.y, width, 115};
      SDL_RenderTexture(renderer, video ? grid->video : thumbnail, NULL, &screen);
    }
    float title_line = slot.y + (thumbnail || video ? 141 : 83);
    text_fit(grid, TILE, song->title, slot.x + 13, title_line, slot.w - 26, white);
    text_fit(grid, FOOTER, song->artist, slot.x + 13, title_line + 24, slot.w - 26, muted);
  }
  int total_rows = (page->count + GRID_COLUMNS - 1) / GRID_COLUMNS;
  if (total_rows > GRID_ROWS) {
    SDL_FRect track = {1387, 156, 4, 554};
    float row_height = track.h / total_rows;
    rectangle(renderer, track, (SDL_Color){210, 156, 243, 56});
    rectangle(renderer, (SDL_FRect){track.x, track.y + row_height * first_row, track.w, row_height * GRID_ROWS}, highlight);
  }
  rectangle(renderer, (SDL_FRect){68, 741, 1300, 1}, (SDL_Color){210, 156, 243, 75});
  if (loading) {
    char label[72];
    snprintf(label, sizeof label, "Starting %s", choice->title);
    text(grid, TILE, status ? status : label, 68, 780, white);
    text(grid, FOOTER, keyboard ? "Esc  Cancel" : "HOME  Cancel", 1208, 780, muted);
    float position = (SDL_GetTicks() % 1440) / 1440.0f;
    rectangle(renderer, (SDL_FRect){68 + 1160 * position, 739, 140, 4}, highlight);
  } else {
    text(grid, FOOTER, keyboard ? "Arrow keys  Browse" : "D-pad  Browse", 68, 780, white);
    const char *action = choice->map ? (keyboard ? "Enter  Play" : "A  Play") : "Browse only";
    text(grid, FOOTER, action, 310, 780, choice->map ? white : muted);
    if (status) text(grid, FOOTER, status, 526, 780, muted);
    else if (!choice->map) text(grid, FOOTER, choice->title, 526, 780, white);
    if (grid->audio || grid->video) text(grid, FOOTER, "Preview playing", 1141, 780, muted);
  }
  SDL_RenderPresent(renderer);
}
