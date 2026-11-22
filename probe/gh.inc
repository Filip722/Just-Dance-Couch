// Greatest Hits starts its song carousel on the Simon Says mode card.
enum GhStage { GH_BOOT, GH_TITLE, GH_MAIN_MENU, GH_CAROUSEL, GH_LOBBY, GH_TRANSITION, GH_DANCE };
static enum GhStage gh_stage;
static uint64_t gh_stage_entered;
static int gh_movie_confirmed, gh_movie_visible, gh_recap;

void gh_probe_track(void) {
  if (!jd2_requested_song() || gh_recap) return;
  const char *track = (const char *)host(cpu.r[3] + 8);
  if (!track) return;
  if (gh_stage == GH_DANCE) {
    if (!strcmp(track, "LevelManager_trk_ScoreBoard")) {
      gh_recap = 1;
      fprintf(stderr, "probe: original Greatest Hits recap entered\n");
      if (jd_couch_game_event) jd_couch_game_event(1);
    }
    return;
  }

  enum GhStage next = gh_stage;
  if (gh_stage == GH_BOOT && !strcmp(track, "Universe_trk_Run")) next = GH_TITLE;
  if (gh_stage == GH_TITLE && !strcmp(track, "MenuPage_Main_trk_Begin")) next = GH_MAIN_MENU;
  if (gh_stage == GH_MAIN_MENU && !strcmp(track, "MenuPage_Flow_trk_Run")) {
    if (!jd2_catalog_ready) {
      fprintf(stderr, "probe: the Greatest Hits catalog could not be prepared\n");
      exit(2);
    }
    next = GH_CAROUSEL;
  }
  if (gh_stage == GH_CAROUSEL && !strcmp(track, "MenuPage_Lobby_trk_CheckInputs")) next = GH_LOBBY;
  if (gh_stage == GH_LOBBY && !strcmp(track, "MenuPage_Lobby_trk_EndLobby")) next = GH_TRANSITION;
  if (!strcmp(track, "LevelManager_trk_GameLoop")) next = GH_DANCE;
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
  enum { FIRST_CARD_MS = 900, RIGHT_RELEASE_MS = 1130, CAROUSEL_SETTLE_MS = 4400, MENU_SETTLE_MS = 1500, CONFIRM_HELD_MS = 330 };
  unsigned elapsed = (time_base() - gh_stage_entered) / JD1_TICKS_PER_MS;
  unsigned settle = gh_stage == GH_CAROUSEL ? CAROUSEL_SETTLE_MS : MENU_SETTLE_MS;
  if (gh_stage == GH_TITLE) settle = 0;
  uint32_t held = 0;
  if (gh_stage == GH_CAROUSEL && elapsed >= FIRST_CARD_MS && elapsed < RIGHT_RELEASE_MS)
    held = BUTTON_RIGHT;
  if (elapsed >= settle && (elapsed - settle) % JD1_CONFIRM_PERIOD_MS < CONFIRM_HELD_MS)
    held = BUTTON_A;
  float x = 0, y = 0.12f;
  if (gh_stage == GH_MAIN_MENU) { x = -0.48f; y = -0.25f; }
  if (gh_stage == GH_LOBBY) y = 0.76f;

  static uint32_t previous;
  const uint32_t menu_buttons = BUTTON_A | BUTTON_RIGHT;
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
  if (key != jd2_song->coach_key) {
    fprintf(stderr, "probe: Greatest Hits opened coach %08x for the requested map %08x\n", key, jd2_song->key);
    exit(2);
  }
  gh_movie_confirmed = 1;
  fprintf(stderr, "probe: original Greatest Hits coach confirmed for map %08x\n", jd2_song->key);
}

void gh_probe_video_frame(void) {
  if (!gh_movie_confirmed || gh_movie_visible) return;
  gh_movie_visible = 1;
  fprintf(stderr, "probe: original Greatest Hits gameplay started\n");
  if (jd_couch_gameplay_ready) jd_couch_gameplay_ready();
}
