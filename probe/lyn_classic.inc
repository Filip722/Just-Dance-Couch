// Both original carousels place a special card before the first routine.
enum GhStage { GH_BOOT, GH_TITLE, GH_MAIN_MENU, GH_CAROUSEL, GH_LOBBY, GH_TRANSITION, GH_DANCE };
static enum GhStage gh_stage;
static uint64_t gh_stage_entered;
enum { GH_MENU_SETTLE_MS = 1500 };
static unsigned gh_first_card_at;
static int gh_movie_confirmed, gh_movie_visible, gh_recap, jd4_scene_confirmed;

void gh_probe_video_frame(void);

void gh_probe_track(void) {
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
  int jd3 = jd2_edition->title_low == SJDE41_TITLE_LOW;
  int jd3_download = jd3 && dlc_index();
  const char *main_menu = jd4 ? "MenuPage_Home_trk_Update" : "MenuPage_Main_trk_Begin";
  const char *carousel = jd4 ? "MenuPage_CoverFlow_trk_PlayAudio" : "MenuPage_Flow_trk_Run";
  const char *lobby = jd4 ? "MenuPage_Lobby_WiiU_CheckInputs" : "MenuPage_Lobby_trk_CheckInputs";
  const char *transition = jd4 ? "InputController_WII_trk_Ingame" : "MenuPage_Lobby_trk_EndLobby";
  enum GhStage next = gh_stage;
  if (gh_stage == GH_BOOT && !strcmp(track, "Universe_trk_Run")) next = GH_TITLE;
  if (gh_stage == GH_TITLE && !strcmp(track, main_menu)) next = GH_MAIN_MENU;
  int entering_lobby = !strcmp(track, lobby);
  // The stock flow can start preview audio or enter the lobby before its run track.
  if (gh_stage == GH_MAIN_MENU &&
      (!strcmp(track, carousel) || (jd3_download && !strcmp(track, "FlowView_trk_PlayAudio")) ||
       entering_lobby)) {
    if (!jd2_catalog_ready) {
      fprintf(stderr, "probe: the %s catalog could not be prepared\n", jd2_edition->name);
      exit(2);
    }
    next = entering_lobby ? GH_LOBBY : GH_CAROUSEL;
  }
  if (gh_stage == GH_CAROUSEL && entering_lobby) next = GH_LOBBY;
  if (gh_stage == GH_LOBBY && !strcmp(track, transition)) next = GH_TRANSITION;
  if (!strcmp(track, "LevelManager_trk_GameLoop")) {
    time_boot_finish();
    next = GH_DANCE;
    if (jd4 || jd3_download) {
      if (!(jd4 ? jd4_scene_confirmed : jd2_map_seen)) {
        fprintf(stderr, "probe: %s did not load the choreography for map %08x\n", jd2_edition->name, jd2_song->key);
        exit(2);
      }
      gh_movie_confirmed = 1;
      gh_probe_video_frame();
    }
  }
  if (next != gh_stage) {
    if (next == GH_LOBBY) fprintf(stderr, "probe: original lobby checking inputs\n");
    gh_stage = next;
    gh_stage_entered = time_base();
  }
}

static int gh_lobby_input(unsigned player, unsigned elapsed, int jd4, uint32_t *held, float *x, float *y) {
  enum {
    GRIP_READY_MS = 4500, CURSOR_READY_MS = 1900,
    AIM_MS = 420, PRESS_MS = 410, WAKE_MS = 2350, CHOOSE_MS = 1600,
  };
  static uint8_t choices[PLAYERS];
  static unsigned count, choose_at;
  if (!count) {
    count = jd_couch_coach_choices ? jd_couch_coach_choices(choices) : 1;
    if (count > 1 && !choices[0] && !choices[1] && !choices[2] && !choices[3]) count = 1;
  }
  if (count == 1) return player == 0;
  if (player && (!choices[player] || !link_linked(player))) return 0;

  unsigned ready = jd4 ? CURSOR_READY_MS : GRIP_READY_MS;
  if (count && !choose_at && elapsed >= ready) choose_at = elapsed;
  unsigned phase = elapsed - choose_at;
  unsigned wake = jd4 ? WAKE_MS : 0;
  if (choose_at && phase >= wake + CHOOSE_MS) return player == 0;

  *held = 0;
  if (!choose_at) {
    // Just Dance 3 and Greatest Hits accept A anywhere on the grip reminder.
    // Aim above the coach cards so these confirmations cannot start a dance.
    *y = -0.60f;
    if (!jd4 && elapsed >= GH_MENU_SETTLE_MS && elapsed < GRIP_READY_MS &&
        (elapsed - GH_MENU_SETTLE_MS) % JD1_CONFIRM_PERIOD_MS < PRESS_MS)
      *held = BUTTON_A;
  } else if (phase < wake) {
    // Just Dance 4 uses the first A to wake a secondary cursor. Wake it over
    // empty space before pressing the original coach widget.
    *y = -0.60f;
    if (player && phase >= AIM_MS && phase < AIM_MS + PRESS_MS) *held = BUTTON_A;
  } else if (choices[player]) {
    *x = (choices[player] - (count + 1) * 0.5f) * (jd4 ? 0.44f : 0.43f);
    *y = jd4 ? 0.35f : 0.03f;
    phase -= wake;
    if (phase >= AIM_MS && phase < AIM_MS + PRESS_MS) *held = BUTTON_A;
  }
  return 1;
}

void gh_probe_input(uint32_t caller, uint32_t channel, uint32_t buffer, uint32_t count) {
  if (!count || channel >= PLAYERS || gh_stage == GH_BOOT || gh_stage >= GH_TRANSITION ||
      (channel && gh_stage != GH_LOBBY) || caller != jd2_edition->input_caller) return;

  // Let the stock transition lay out the cards. Right moves from the mode
  // card to the first routine, which the verified catalog swap selected.
  enum {
    FIRST_CARD_MS = 900, RIGHT_HELD_MS = 230, CAROUSEL_SETTLE_MS = 4400,
    CONFIRM_HELD_MS = 330,
  };
  unsigned elapsed = (time_base() - gh_stage_entered) / JD1_TICKS_PER_MS;
  unsigned settle = gh_stage == GH_CAROUSEL ? CAROUSEL_SETTLE_MS : GH_MENU_SETTLE_MS;
  if (gh_stage == GH_TITLE) settle = 0;
  int jd4 = jd2_edition->title_low == JD4_TITLE_LOW;
  int jd3_download = jd2_edition->title_low == SJDE41_TITLE_LOW && dlc_index();
  uint32_t held = 0;
  // Just Dance 3 centers the mounted routine when its DLC carousel opens.
  if (!jd4 && !jd3_download && gh_stage == GH_CAROUSEL && elapsed >= FIRST_CARD_MS) {
    // Start the move and its settling time from the first available sample.
    if (!gh_first_card_at) gh_first_card_at = elapsed;
    if (elapsed - gh_first_card_at < RIGHT_HELD_MS) held = BUTTON_RIGHT;
    settle += gh_first_card_at - FIRST_CARD_MS;
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
      x = -0.51f;
      y = -0.19f;
    }
  } else if (gh_stage == GH_MAIN_MENU) {
    x = -0.48f;
    y = -0.25f;
  }
  if (!jd4 && gh_stage == GH_LOBBY) y = 0.76f;
  if (gh_stage == GH_LOBBY && !gh_lobby_input(channel, elapsed, jd4, &held, &x, &y)) return;

  static uint32_t previous[PLAYERS];
  // The scripted Just Dance 3 and Greatest Hits menus own navigation until the dance starts.
  const uint32_t menu_buttons = jd4 ? (BUTTON_A | BUTTON_RIGHT) : UINT32_MAX;
  for (uint32_t i = 0; i < count && i < JD2_KPAD_MAX_SAMPLES; i++) {
    uint32_t pad = buffer + i * JD2_KPAD_STATUS_BYTES;
    wr32(pad + JD1_KPAD_HOLD, (rd32(pad + JD1_KPAD_HOLD) & ~menu_buttons) | held);
    wr32(pad + JD1_KPAD_TRIGGER, (rd32(pad + JD1_KPAD_TRIGGER) & ~menu_buttons) | (held & ~previous[channel]));
    wr32(pad + JD1_KPAD_RELEASE, (rd32(pad + JD1_KPAD_RELEASE) & ~menu_buttons) | (previous[channel] & ~held));
    wr32(pad + JD1_KPAD_POINTER, float_bits(x));
    wr32(pad + JD1_KPAD_POINTER + 4, float_bits(y));
    wr8(pad + JD1_KPAD_POINTER_VALID, JD1_POINTER_SEEN);
  }
  previous[channel] = held;
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
