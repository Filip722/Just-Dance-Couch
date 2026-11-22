// Just Dance 2 uses the original catalog order to choose its first song.
enum {
  JD2_CATALOG_BYTES = 80402,
  JD2_FIRST_SLOT_OFFSET = 37411,
  JD2_INPUT_CALLER = 0x80030048,
  JD2_LOAD_GROUP_CALLER = 0x80144d74,
  JD2_KPAD_STATUS_BYTES = 240,
  JD2_KPAD_MAX_SAMPLES = 16,
  JD2_MENU_SETTLE_MS = 2000,
};

typedef struct {
  uint32_t key, index_offset;
  uint8_t index;
} Jd2SongSlot;

// Generated from the verified USA catalog in probe/jd2.py.
static const Jd2SongSlot jd2_song_slots[] = {
JD2_SONG_SLOTS
};

static const Jd2SongSlot *jd2_song;
static int jd2_request_read, jd2_catalog_ready, jd2_title_ready, jd2_map_seen, jd2_playing, jd2_recap_seen;
static uint64_t jd2_menu_ready;

static const Jd2SongSlot *jd2_requested_song(void) {
  if (jd2_request_read) return jd2_song;
  jd2_request_read = 1;
  const char *map = getenv("JD2_PROBE_MAP");
  if (!map) return NULL;
  char *end;
  unsigned long key = strtoul(map, &end, 16);
  for (size_t i = 0; !*end && i < sizeof jd2_song_slots / sizeof *jd2_song_slots; i++) {
    if (jd2_song_slots[i].key == key) {
      jd2_song = &jd2_song_slots[i];
      return jd2_song;
    }
  }
  fprintf(stderr, "probe: unknown Just Dance 2 song %s\n", map);
  exit(2);
}

void jd2_probe_catalog(uint32_t buffer, uint32_t bytes) {
  if (bytes != JD2_CATALOG_BYTES || jd2_catalog_ready || !jd2_requested_song()) return;
  // The first two words identify the original catalog's world record.
  if (rd32(buffer) != 0x6a0e808cu || rd32(buffer + 4) != 0x02050000u) return;
  uint32_t first = buffer + JD2_FIRST_SLOT_OFFSET;
  uint32_t selected = buffer + jd2_song->index_offset;
  uint32_t first_index = rd32(first), selected_index = rd32(selected);
  if (first_index != __builtin_bswap32(1) || selected_index != __builtin_bswap32(jd2_song->index)) {
    fprintf(stderr, "probe: unexpected Just Dance 2 catalog for %08x\n", jd2_song->key);
    exit(2);
  }
  wr32(first, selected_index);
  wr32(selected, first_index);
  jd2_catalog_ready = 1;
  if (jd_couch_library_ready) jd_couch_library_ready();
}

void jd2_probe_track(void) {
  if (!jd2_requested_song() || jd2_recap_seen) return;
  const char *track = (const char *)host(cpu.r[3] + 8);
  if (!track) return;
  if (jd2_playing) {
    if (!strcmp(track, "WD2_LevelManager_trk_ScoreBoard")) {
      jd2_recap_seen = 1;
      fprintf(stderr, "probe: original Just Dance 2 recap entered\n");
      if (jd_couch_game_event) jd_couch_game_event(1);
    }
    return;
  }
  if (!jd2_title_ready && !strcmp(track, "Universe_trk_Run")) jd2_title_ready = 1;
  if (!jd2_menu_ready && !strcmp(track, "WD2_MenuOut_trk_RefreshCoverflow")) {
    if (!jd2_catalog_ready) {
      fprintf(stderr, "probe: the Just Dance 2 catalog could not be prepared\n");
      exit(2);
    }
    jd2_menu_ready = time_base();
  }
  if (!strcmp(track, "WD2_LevelManager_trk_GameLoop")) {
    if (!jd2_map_seen) {
      fprintf(stderr, "probe: the Just Dance 2 loader did not confirm the selected song\n");
      exit(2);
    }
    jd2_playing = 1;
    fprintf(stderr, "probe: original Just Dance 2 gameplay started\n");
    if (jd_couch_gameplay_ready) jd_couch_gameplay_ready();
  }
}

void jd2_probe_input(uint32_t caller, uint32_t channel, uint32_t buffer, uint32_t count) {
  if (caller != JD2_INPUT_CALLER || channel || !count || !jd2_title_ready || jd2_playing) return;
  uint64_t now = time_base();
  int menu = jd2_menu_ready && now - jd2_menu_ready >= (uint64_t)JD2_MENU_SETTLE_MS * JD1_TICKS_PER_MS;
  uint64_t milliseconds = now / JD1_TICKS_PER_MS;
  int held = milliseconds % JD1_CONFIRM_PERIOD_MS < JD1_CONFIRM_HELD_MS && (!jd2_menu_ready || menu);
  static int previous;
  // KPAD returns a batch of statuses. The game reads more than the first.
  for (uint32_t i = 0; i < count && i < JD2_KPAD_MAX_SAMPLES; i++) {
    uint32_t pad = buffer + i * JD2_KPAD_STATUS_BYTES;
    wr32(pad + JD1_KPAD_HOLD, (rd32(pad + JD1_KPAD_HOLD) & ~BUTTON_A) | (held ? BUTTON_A : 0));
    wr32(pad + JD1_KPAD_TRIGGER, (rd32(pad + JD1_KPAD_TRIGGER) & ~BUTTON_A) | (held && !previous ? BUTTON_A : 0));
    wr32(pad + JD1_KPAD_RELEASE, (rd32(pad + JD1_KPAD_RELEASE) & ~BUTTON_A) | (!held && previous ? BUTTON_A : 0));
    if (menu) {
      wr32(pad + JD1_KPAD_POINTER, float_bits(0.0f));
      wr32(pad + JD1_KPAD_POINTER + 4, float_bits(-0.56f));
      wr8(pad + JD1_KPAD_POINTER_VALID, JD1_POINTER_SEEN);
    }
  }
  previous = held;
}

void jd2_probe_map(void) {
  if (!jd2_catalog_ready || jd2_map_seen || cpu.lr != JD2_LOAD_GROUP_CALLER) return;
  for (size_t i = 0; i < sizeof jd2_song_slots / sizeof *jd2_song_slots; i++) {
    if (jd2_song_slots[i].key != cpu.r[4]) continue;
    if (cpu.r[4] != jd2_song->key) {
      fprintf(stderr, "probe: Just Dance 2 selected %08x instead of %08x\n", cpu.r[4], jd2_song->key);
      exit(2);
    }
    jd2_map_seen = 1;
    fprintf(stderr, "probe: original map loader requested %08x\n", jd2_song->key);
    return;
  }
}
