// Land Ho! titles share the Bink player and use numeric rows for their routines.
static const char *family_movie;
static uint32_t family_song;
static uint32_t family_playback;
static int family_recap;

int family_probe_active(void) {
  return couch_booting() && !family_playback;
}

int family_probe_select(uint32_t menu) {
  if (!menu || !family_probe_active()) return 0;
  if (!family_movie) {
    const char *map = jd_couch_wait_for_map();
    if (!map) return 0;
    char *end;
    unsigned long row = strtoul(map, &end, 10);
    if (end == map || *end != '-' || !end[1] || row > UINT32_MAX) exit(2);
    family_song = row;
    family_movie = end + 1;
  }
  return 1;
}

uint32_t family_probe_song(uint32_t original) {
  return family_probe_active() && family_movie ? family_song : original;
}

void family_probe_strap(void) {
  if (!family_probe_active()) return;
  unsigned state = rd32(cpu.r[3] + 24);
  if (state == 1 || state == 2) wr32(cpu.r[3] + 20, 0);
}

void family_probe_opening(unsigned movie_offset) {
  if (!family_probe_active()) return;
  uint32_t task = cpu.r[3];
  unsigned step = rd32(task + 28);
  if (rd32(task + 20) == 0 && rd32(task + movie_offset) && step <= 1)
    wr32(task + 28, 2);  // Finish the logo movie via the original teardown.
  if (rd32(task + 20) != 0 && step == 1) wr32(task + 24, 0);
}

void family_probe_title(void) {
  // The launcher's confirmation leaves the optional first-run survey at its default.
  if (family_probe_active() && rd32(cpu.r[3] + 24) == 2) wr32(cpu.r[3] + 24, 3);
}

int family_probe_player(unsigned slot) {
  return slot == 0 || link_linked(slot) ? (int)slot : -1;
}

void family_probe_video(void) {
  if (!family_movie) return;
  uint32_t movie = cpu.r[3];
  if (!family_playback && rd32(movie + 40) > 1) {
    const char *path = (const char *)host(movie + 68);
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    size_t length = strlen(family_movie);
    if (strlen(name) != length + 4 || strncasecmp(name, family_movie, length)
        || strcasecmp(name + length, ".bik")) return;
    family_playback = movie;
    time_boot_finish();
    fprintf(stderr, "probe: playing original family routine %s\n", name);
    jd_couch_game_event(0);
    jd_couch_gameplay_ready();
  }
  if (!family_recap && family_playback == movie && !rd32(movie + 56)) {
    family_recap = 1;
    jd_couch_game_event(1);
  }
}
