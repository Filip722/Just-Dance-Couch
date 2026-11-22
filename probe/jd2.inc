// Both games choose the first song in their original catalog order.
enum {
  JD2_TITLE_LOW = 0x53443259,
  SUMMER_TITLE_LOW = 0x534a3945,
  GH_TITLE_LOW = 0x534a4845,
  JD4_TITLE_LOW = 0x534a5845,
  JD2_KPAD_STATUS_BYTES = 240,
  JD2_KPAD_MAX_SAMPLES = 16,
  JD2_MENU_SETTLE_MS = 2000,
};

typedef struct {
  uint32_t key, slot_offset, slot, asset_key;
} Jd2SongSlot;

// Generated from the verified USA catalog in probe/jd2.py.
static const Jd2SongSlot jd2_song_slots[] = {
JD2_SONG_SLOTS
};
static const Jd2SongSlot summer_song_slots[] = {
SUMMER_SONG_SLOTS
};
static const Jd2SongSlot gh_song_slots[] = {
GH_SONG_SLOTS
};
static const Jd2SongSlot jd3_song_slots[] = {
JD3_SONG_SLOTS
};
static const Jd2SongSlot jd4_song_slots[] = {
JD4_SONG_SLOTS
};
static const Jd2SongSlot jd2_dlc_slots[] = {
JD2_DLC_SLOTS
};
static const Jd2SongSlot jd3_dlc_slots[] = {
JD3_DLC_SLOTS
};
static const Jd2SongSlot jd4_dlc_slots[] = {
JD4_DLC_SLOTS
};

typedef struct {
  const char *name;
  uint32_t title_low, catalog_bytes, catalog_signature, first_slot_offset, first_slot;
  uint32_t input_caller, load_group_caller;
  const Jd2SongSlot *slots;
  size_t count;
} Jd2Edition;

static const Jd2Edition jd2_editions[] = {
  {
    .name = "Just Dance 2", .title_low = JD2_TITLE_LOW,
    .catalog_bytes = 80402, .catalog_signature = 0x02050000u,
    .first_slot_offset = 37411, .first_slot = 1,
    .input_caller = 0x80030048u, .load_group_caller = 0x80144d74u,
    .slots = jd2_song_slots, .count = sizeof jd2_song_slots / sizeof *jd2_song_slots,
  },
  {
    .name = "Just Dance Summer Party", .title_low = SUMMER_TITLE_LOW,
    .catalog_bytes = 33732, .catalog_signature = 0xca030000u,
    .first_slot_offset = 702, .first_slot = 0x0f304892u,
    .input_caller = 0x8002305cu, .load_group_caller = 0x801236ecu,
    .slots = summer_song_slots, .count = sizeof summer_song_slots / sizeof *summer_song_slots,
  },
  {
    .name = "Just Dance Greatest Hits", .title_low = GH_TITLE_LOW,
    .catalog_bytes = 60005, .catalog_signature = 0x92040000u,
    .first_slot_offset = 722, .first_slot = 0x233018b9u,
    .input_caller = 0x8002eb30u,
    .slots = gh_song_slots, .count = sizeof gh_song_slots / sizeof *gh_song_slots,
  },
  {
    .name = "Just Dance 3", .title_low = SJDE41_TITLE_LOW,
    .catalog_bytes = 144922, .catalog_signature = 0x5a060000u,
    .first_slot_offset = 722, .first_slot = 0x72c1ce4eu,
    .input_caller = 0x80036110u,
    .slots = jd3_song_slots, .count = sizeof jd3_song_slots / sizeof *jd3_song_slots,
  },
  {
    .name = "Just Dance 4", .title_low = JD4_TITLE_LOW,
    .catalog_bytes = 0x3fffcu, .catalog_signature = 0x00ba0b03u,
    .first_slot_offset = 0xc963, .first_slot = 0x2dc2573fu,
    .input_caller = 0x8006ddfcu,
    .slots = jd4_song_slots, .count = sizeof jd4_song_slots / sizeof *jd4_song_slots,
  },
};

static const Jd2Edition *jd2_edition;
static const Jd2SongSlot *jd2_song;
static int jd2_catalog_ready, jd2_title_ready, jd2_map_seen, jd2_playing, jd2_recap_seen;
static int jd2_download_slot_ready;
static uint64_t jd2_menu_ready;

static const Jd2Edition *jd2_title_edition(void) {
  for (size_t i = 0; i < sizeof jd2_editions / sizeof *jd2_editions; i++)
    if (jd2_editions[i].title_low == title->title_low) return &jd2_editions[i];
  return NULL;
}

static const Jd2SongSlot *jd2_requested_song(void) {
  if (jd2_song) return jd2_song;
  const char *map = legacy_boot_map("JD2_PROBE_MAP");
  if (!map) return NULL;
  char *end;
  unsigned long key = strtoul(map, &end, 16);
  const Jd2SongSlot *slots = jd2_edition->slots;
  size_t count = jd2_edition->count;
  if (dlc_index()) {
    switch (jd2_edition->title_low) {
      case JD2_TITLE_LOW: slots = jd2_dlc_slots; count = sizeof jd2_dlc_slots / sizeof *jd2_dlc_slots; break;
      case SJDE41_TITLE_LOW: slots = jd3_dlc_slots; count = sizeof jd3_dlc_slots / sizeof *jd3_dlc_slots; break;
      case JD4_TITLE_LOW: slots = jd4_dlc_slots; count = sizeof jd4_dlc_slots / sizeof *jd4_dlc_slots; break;
    }
  }
  for (size_t i = 0; map != end && !*end && i < count; i++) {
    if (slots[i].key == key) {
      jd2_song = &slots[i];
      return jd2_song;
    }
  }
  fprintf(stderr, "probe: unknown %s song %s\n", jd2_edition->name, map);
  exit(2);
}

static void jd3_unlock_catalog_bonuses(uint32_t buffer) {
  // Keep these on-disc routines in Classic by clearing their Mojo bonus keys.
  // Each ma_BonusProperties entry contains the Mojo level, map key and reward type.
  static const char bonus_properties[] = "ma_BonusProperties";
  static const struct { uint32_t offset, key, mojo; } bonuses[] = {
    {0x6c0c, 0x2dc08999u, 2},   // Baby Zouk
    {0x6c74, 0x2dc08a17u, 12},  // Mamasita
    {0x6cdc, 0x2dc0895au, 22},  // The Master Blaster
    {0x6d10, 0x2dc0f7b7u, 27},  // Hungarian Dance No. 5
  };
  const char *name = (const char *)host(buffer + 0x6b94);
  if (!name || memcmp(name, bonus_properties, sizeof bonus_properties)) return;
  for (size_t i = 0; i < sizeof bonuses / sizeof *bonuses; i++) {
    uint32_t map = buffer + bonuses[i].offset;
    if (rd32(map) == __builtin_bswap32(bonuses[i].key) &&
        rd32(map - 4) == __builtin_bswap32(bonuses[i].mojo) && rd32(map + 4) == 0x01000000u)
      wr32(map, 0);
  }
}

void jd2_probe_catalog(uint32_t buffer, uint32_t bytes) {
  // Greatest Hits hides two on-disc routines behind title-screen codes.
  // Let the native bonus check accept them in the worker that launches one.
  enum {
    GH_BABY_GIRL = 0x76c053e8u, GH_KATTI_KALANDAL = 0x76c05471u,
    GH_BONUS_SCRIPT_BYTES = 144697, GH_BONUS_UNLOCK_METHOD = 54689,
    GH_BONUS_LOCKED_RETURN = 55173,
    JD3_BONUS_BLOCK_BYTES = 0x3fffc, JD3_DLC_CATALOG_BYTES = 75017,
  };
  if (title->title_low == GH_TITLE_LOW && bytes == GH_BONUS_SCRIPT_BYTES && jd2_song &&
      (jd2_song->key == GH_BABY_GIRL || jd2_song->key == GH_KATTI_KALANDAL)) {
    static const char unlock_method[] = "BonusManager_IsBonusUnlocked";
    const char *name = (const char *)host(buffer + GH_BONUS_UNLOCK_METHOD);
    uint32_t locked = buffer + GH_BONUS_LOCKED_RETURN;
    // Replace the verified LyN bytecode's false return literal with true.
    if (name && !memcmp(name, unlock_method, sizeof unlock_method) &&
        rd32(locked) == 0x24080000u && rd32(locked + 4) == 0x14000000u && !rd32(locked + 8))
      wr8(locked + 6, 1);
    return;
  }
  if (title->title_low == SJDE41_TITLE_LOW) {
    if (dlc_index()) {
      if (jd2_catalog_ready && !jd2_map_seen && bytes >= 4 &&
          rd32(buffer) == __builtin_bswap32(jd2_song->asset_key)) {
        jd2_map_seen = 1;
        fprintf(stderr, "probe: original Just Dance 3 DLC choreography confirmed for map %08x\n", jd2_song->key);
      }
    } else if (bytes == JD3_BONUS_BLOCK_BYTES && rd32(buffer) == 0x53993109u &&
               rd32(buffer + 4) == 0xda040000u && (couch_booting() || getenv("JD2_PROBE_MAP"))) {
      // This world can load before the picker has received a selection.
      jd3_unlock_catalog_bonuses(buffer);
      return;
    }
  }
  if (jd2_catalog_ready) return;
  if (!jd2_edition) jd2_edition = jd2_title_edition();
  if (dlc_index() && title->title_low == SJDE41_TITLE_LOW && bytes == JD3_DLC_CATALOG_BYTES &&
      rd32(buffer) == 0x365e80c4u && rd32(buffer + 4) == 0x92040000u && jd2_requested_song()) {
    jd2_download_slot_ready = rd32(buffer + jd2_song->slot_offset) == __builtin_bswap32(jd2_song->slot);
    return;
  }
  if (dlc_index() && title->title_low == JD2_TITLE_LOW && bytes == 40799 &&
      rd32(buffer) == 0xca0a808cu && rd32(buffer + 4) == 0xfa030000u && jd2_requested_song()) {
    uint32_t slot = buffer + jd2_song->slot_offset;
    if (rd32(slot) == __builtin_bswap32(jd2_song->slot)) {
      wr32(slot, __builtin_bswap32(jd2_edition->first_slot));
      jd2_download_slot_ready = 1;
    }
    return;
  }
  if (!jd2_edition || bytes != jd2_edition->catalog_bytes) return;
  // The first two words identify the original catalog's world record.
  if (rd32(buffer) != 0x6a0e808cu || rd32(buffer + 4) != jd2_edition->catalog_signature) return;
  // These menus need their established Wii Remote cadence after the catalog loads.
  if (jd2_edition->title_low == SJDE41_TITLE_LOW || jd2_edition->title_low == GH_TITLE_LOW)
    time_boot_controller_menu();
  if (!jd2_requested_song()) return;
  uint32_t first = buffer + jd2_edition->first_slot_offset;
  if (dlc_index()) {
    if (!jd2_download_slot_ready || rd32(first) != __builtin_bswap32(jd2_edition->first_slot)) return;
    wr32(first, __builtin_bswap32(jd2_song->slot));
    jd2_catalog_ready = 1;
    fprintf(stderr, "probe: selected %s DLC slot for %08x\n", jd2_edition->name, jd2_song->key);
    return;
  }
  uint32_t selected = buffer + jd2_song->slot_offset;
  uint32_t first_slot = rd32(first), selected_slot = rd32(selected);
  if (first_slot != __builtin_bswap32(jd2_edition->first_slot) ||
      selected_slot != __builtin_bswap32(jd2_song->slot)) {
    fprintf(stderr, "probe: unexpected %s catalog for %08x\n", jd2_edition->name, jd2_song->key);
    exit(2);
  }
  wr32(first, selected_slot);
  wr32(selected, first_slot);
  jd2_catalog_ready = 1;
}

void jd2_probe_track(void) {
  if ((!couch_booting() && !jd2_song && !getenv("JD2_PROBE_MAP")) || jd2_recap_seen) return;
  if (!jd2_edition) jd2_edition = jd2_title_edition();
  const char *track = (const char *)host(cpu.r[3] + 8);
  if (!track) return;
  if (jd2_playing) {
    if (!strcmp(track, "WD2_LevelManager_trk_ScoreBoard")) {
      jd2_recap_seen = 1;
      fprintf(stderr, "probe: original %s recap entered\n", jd2_edition->name);
      if (jd_couch_game_event) jd_couch_game_event(1);
    }
    return;
  }
  if (!jd2_title_ready && !strcmp(track, "Universe_trk_Run")) jd2_title_ready = 1;
  if (!jd2_menu_ready && !strcmp(track, "WD2_MenuOut_trk_RefreshCoverflow")) {
    if (!jd2_catalog_ready) {
      fprintf(stderr, "probe: the %s catalog could not be prepared\n", jd2_edition->name);
      exit(2);
    }
    jd2_menu_ready = time_base();
  }
  if (!strcmp(track, "WD2_LevelManager_trk_GameLoop")) {
    if (!jd2_map_seen) {
      fprintf(stderr, "probe: the %s loader did not confirm the selected song\n", jd2_edition->name);
      exit(2);
    }
    time_boot_finish();
    jd2_playing = 1;
    fprintf(stderr, "probe: original %s gameplay started\n", jd2_edition->name);
    if (jd_couch_gameplay_ready) jd_couch_gameplay_ready();
  }
}

void jd2_probe_input(uint32_t caller, uint32_t channel, uint32_t buffer, uint32_t count) {
  if (channel || !count || !jd2_title_ready || jd2_playing || caller != jd2_edition->input_caller) return;
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
  if (!jd2_catalog_ready || jd2_map_seen || cpu.lr != jd2_edition->load_group_caller) return;
  if (dlc_index() && cpu.r[4] == jd2_song->key) {
    jd2_map_seen = 1;
    fprintf(stderr, "probe: original map loader requested %08x\n", jd2_song->key);
    return;
  }
  for (size_t i = 0; i < jd2_edition->count; i++) {
    if (jd2_edition->slots[i].key != cpu.r[4]) continue;
    if (cpu.r[4] != jd2_song->key) {
      fprintf(stderr, "probe: %s selected %08x instead of %08x\n", jd2_edition->name, cpu.r[4], jd2_song->key);
      exit(2);
    }
    jd2_map_seen = 1;
    fprintf(stderr, "probe: original map loader requested %08x\n", jd2_song->key);
    return;
  }
}
