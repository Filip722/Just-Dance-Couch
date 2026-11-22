// Both original carousels place a special card before the first routine.
enum GhStage { GH_BOOT, GH_TITLE, GH_MAIN_MENU, GH_CAROUSEL, GH_LOBBY, GH_TRANSITION, GH_DANCE };
static enum GhStage gh_stage;
static uint64_t gh_stage_entered;
static unsigned gh_first_card_at;
static int gh_movie_confirmed, gh_movie_visible, gh_recap, jd4_scene_confirmed;

void gh_probe_video_frame(void);

void gh_probe_track(void) {
  if (dlc_index() && title->title_low == SJDE41_TITLE_LOW) return;
  if ((!couch_booting() && !jd2_song && !getenv("JD2_PROBE_MAP")) || gh_recap) return;
  if (!jd2_edition) jd2_edition = jd2_title_edition();
  const char *track = (const char *)host(cpu.r[3] + 8);
  if (!track) return;
  if (gh_stage == GH_DANCE) {
    if (!strcmp(track, "LevelManager_trk_ScoreBoard")) {
      gh_recap = 1;
      fprintf(stderr, "probe: original %s recap entered\n", jd2_edition->name);
      if (jd_couch_game_event) jd_couch_game_event(1);
    }
    return;
  }

  int jd4 = jd2_edition->title_low == JD4_TITLE_LOW;
  const char *main_menu = jd4 ? "MenuPage_Home_trk_Update" : "MenuPage_Main_trk_Begin";
  const char *carousel = jd4 ? "MenuPage_CoverFlow_trk_PlayAudio" : "MenuPage_Flow_trk_Run";
  const char *lobby = jd4 ? "MenuPage_Lobby_WiiU_CheckInputs" : "MenuPage_Lobby_trk_CheckInputs";
  const char *transition = jd4 ? "InputController_WII_trk_Ingame" : "MenuPage_Lobby_trk_EndLobby";
  enum GhStage next = gh_stage;
  if (gh_stage == GH_BOOT && !strcmp(track, "Universe_trk_Run")) next = GH_TITLE;
  if (gh_stage == GH_TITLE && !strcmp(track, main_menu)) next = GH_MAIN_MENU;
  if (gh_stage == GH_MAIN_MENU && !strcmp(track, carousel)) {
    if (!jd2_catalog_ready) {
      fprintf(stderr, "probe: the %s catalog could not be prepared\n", jd2_edition->name);
      exit(2);
    }
    next = GH_CAROUSEL;
  }
  if (gh_stage == GH_CAROUSEL && !strcmp(track, lobby)) next = GH_LOBBY;
  if (gh_stage == GH_LOBBY && !strcmp(track, transition)) {
    time_boot_finish();
    next = GH_TRANSITION;
  }
  if (!strcmp(track, "LevelManager_trk_GameLoop")) {
    time_boot_finish();
    next = GH_DANCE;
    if (jd4) {
      if (!jd4_scene_confirmed) {
        fprintf(stderr, "probe: Just Dance 4 did not load the choreography for map %08x\n", jd2_song->key);
        exit(2);
      }
      gh_movie_confirmed = 1;
      gh_probe_video_frame();
    }
  }
  if (next != gh_stage) {
    gh_stage = next;
    gh_stage_entered = time_base();
  }
}

void gh_probe_input(uint32_t caller, uint32_t channel, uint32_t buffer, uint32_t count) {
  if (channel || !count || gh_stage == GH_BOOT || gh_stage >= GH_TRANSITION ||
      caller != jd2_edition->input_caller) return;

  // Let the stock transition lay out the cards. Right moves from the mode
  // card to the first routine, which the verified catalog swap selected.
  enum {
    FIRST_CARD_MS = 900, RIGHT_HELD_MS = 230, CAROUSEL_SETTLE_MS = 4400,
    MENU_SETTLE_MS = 1500, CONFIRM_HELD_MS = 330, JD3_PRIVACY_PROMPT_MS = 5400,
  };
  unsigned elapsed = (time_base() - gh_stage_entered) / JD1_TICKS_PER_MS;
  unsigned settle = gh_stage == GH_CAROUSEL ? CAROUSEL_SETTLE_MS : MENU_SETTLE_MS;
  if (gh_stage == GH_TITLE) settle = 0;
  int jd4 = jd2_edition->title_low == JD4_TITLE_LOW;
  int greatest_hits = jd2_edition->title_low == GH_TITLE_LOW;
  uint32_t held = 0;
  if (!jd4 && gh_stage == GH_CAROUSEL && elapsed >= FIRST_CARD_MS) {
    unsigned press_at = FIRST_CARD_MS;
    if (greatest_hits) {
      // Send the move on the first available sample and let the card settle
      // from that point even if the remote paused during the usual window.
      if (!gh_first_card_at) gh_first_card_at = elapsed;
      press_at = gh_first_card_at;
      settle += press_at - FIRST_CARD_MS;
    }
    if (elapsed - press_at < RIGHT_HELD_MS) held = BUTTON_RIGHT;
  }
  if (elapsed >= settle && (elapsed - settle) % JD1_CONFIRM_PERIOD_MS < CONFIRM_HELD_MS)
    held = BUTTON_A;
  float x = 0, y = 0.12f;
  if (jd4) {
    if (gh_stage == GH_TITLE) y = 0.58f;
    if (gh_stage == GH_MAIN_MENU) { x = -0.45f; y = 0; }
    if (gh_stage == GH_CAROUSEL) x = -0.48f;
    if (gh_stage == GH_LOBBY) y = 0.80f;
  } else if (jd2_edition->title_low == SJDE41_TITLE_LOW) {
    if (gh_stage == GH_TITLE) y = 0.58f;
    if (gh_stage == GH_MAIN_MENU) {
      // The first-run opt-out is in the lower right of the stock prompt.
      x = elapsed < JD3_PRIVACY_PROMPT_MS ? 0.76f : -0.51f;
      y = elapsed < JD3_PRIVACY_PROMPT_MS ? 0.53f : -0.19f;
    }
  } else if (gh_stage == GH_MAIN_MENU) {
    x = -0.48f;
    y = -0.25f;
  }
  if (!jd4 && gh_stage == GH_LOBBY) y = 0.76f;

  static uint32_t previous;
  // Greatest Hits' hidden menu sequence owns navigation until the dance starts.
  const uint32_t menu_buttons = greatest_hits ? UINT32_MAX : BUTTON_A | BUTTON_RIGHT;
  for (uint32_t i = 0; i < count && i < JD2_KPAD_MAX_SAMPLES; i++) {
    uint32_t pad = buffer + i * JD2_KPAD_STATUS_BYTES;
    wr32(pad + JD1_KPAD_HOLD, (rd32(pad + JD1_KPAD_HOLD) & ~menu_buttons) | held);
    wr32(pad + JD1_KPAD_TRIGGER, (rd32(pad + JD1_KPAD_TRIGGER) & ~menu_buttons) | (held & ~previous));
    wr32(pad + JD1_KPAD_RELEASE, (rd32(pad + JD1_KPAD_RELEASE) & ~menu_buttons) | (previous & ~held));
    wr32(pad + JD1_KPAD_POINTER, float_bits(x));
    wr32(pad + JD1_KPAD_POINTER + 4, float_bits(y));
    wr8(pad + JD1_KPAD_POINTER_VALID, JD1_POINTER_SEEN);
  }
  previous = held;
}

void gh_probe_movie(void) {
  if (gh_stage != GH_DANCE || gh_movie_confirmed) return;
  // BinkOpenFile takes the coach key from the native texture object's field.
  enum { COACH_KEY_OFFSET = 340 };
  uint32_t key = rd32(cpu.r[3] + COACH_KEY_OFFSET);
  if (key != jd2_song->asset_key) {
    fprintf(stderr, "probe: %s opened coach %08x for map %08x\n", jd2_edition->name, key, jd2_song->key);
    exit(2);
  }
  gh_movie_confirmed = 1;
  fprintf(stderr, "probe: original %s coach confirmed for map %08x\n", jd2_edition->name, jd2_song->key);
}

void gh_probe_video_frame(void) {
  if (!gh_movie_confirmed || gh_movie_visible) return;
  gh_movie_visible = 1;
  fprintf(stderr, "probe: original %s gameplay started\n", jd2_edition->name);
  if (jd_couch_gameplay_ready) jd_couch_gameplay_ready();
}


void jd4_probe_album(uint32_t buffer, uint32_t bytes) {
  if (!jd2_edition) jd2_edition = jd2_title_edition();
  if (!jd2_edition || bytes != jd2_edition->catalog_bytes || rd32(buffer) ||
      rd32(buffer + 4) != jd2_edition->catalog_signature || !jd2_requested_song()) return;

  // These records are consecutive calls to GameCatalog_CreateAlbum.
  // Keep each original argument list and invocation intact as it moves.
  enum { ALBUM_BYTES = 104, ALBUM_KEY_OFFSET = 6 };
  uint32_t first = buffer + jd2_edition->first_slot_offset;
  if (dlc_index()) {
    // Point the original album's routine references at the mounted download.
    uint32_t previous = __builtin_bswap32(jd2_edition->first_slot);
    if (rd32(first) != previous || rd32(first - ALBUM_KEY_OFFSET) != 0x23080000u) return;
    for (unsigned i = 0; i < ALBUM_BYTES - ALBUM_KEY_OFFSET - 3; i++)
      if (rd32(first + i) == previous) wr32(first + i, __builtin_bswap32(jd2_song->key));
    jd2_catalog_ready = 1;
    fprintf(stderr, "probe: selected Just Dance 4 DLC album for %08x\n", jd2_song->key);
    return;
  }
  uint32_t selected = buffer + jd2_song->slot_offset;
  if (rd32(first) != __builtin_bswap32(jd2_edition->first_slot) ||
      rd32(selected) != __builtin_bswap32(jd2_song->slot) ||
      rd32(first - ALBUM_KEY_OFFSET) != 0x23080000u ||
      rd32(selected - ALBUM_KEY_OFFSET) != 0x23080000u) return;

  for (uint32_t i = 0; i < ALBUM_BYTES; i++) {
    uint32_t a = first - ALBUM_KEY_OFFSET + i, b = selected - ALBUM_KEY_OFFSET + i;
    uint8_t saved = rd8(a);
    wr8(a, rd8(b));
    wr8(b, saved);
  }
  jd2_catalog_ready = 1;
}

void jd4_probe_scene(void) {
  // BIG_BinDecompressBlock opens the selected routine's choreography scene.
  enum { CHOREOGRAPHY_CALLER = 0x800738f4 };
  if (gh_stage < GH_CAROUSEL || jd4_scene_confirmed || cpu.lr != CHOREOGRAPHY_CALLER) return;
  if (rd32(cpu.r[5]) == jd2_song->asset_key) {
    jd4_scene_confirmed = 1;
    fprintf(stderr, "probe: original Just Dance 4 choreography confirmed for map %08x\n", jd2_song->key);
  }
}
