// Guest entry points and layouts from the SJOE41 revision 0 debug ELF.
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include "cpu.h"
#include "input.h"
#include "title.h"

enum {
  DATABASE_SDA_OFFSET = 0x1700,
  ACTIVE_MODULE_OFFSET = 0xd0,
  MODULE_MAIN_MENU = 2,
  GAME_MANAGER_SDA_OFFSET = 0x14b0,
  DESCRIPTOR_MAP_NAME = 0x5c,
  SONG_MODE_CLASSIC = 0,
  SONG_VERSION_DISC = 1,
  MODULE_GAMEPLAY_CLASSIC = 8,
  SJOE41_TITLE_LOW = 0x534a4f45,
  SPLASH_INITIALIZED = 0, SPLASH_DISPLAYING = 4, SPLASH_PAUSED = 8,
};

static const uint32_t DIRECT_LAUNCH_CALLER = 0x801d3c40u;
static const uint32_t MANAGER_CONNECTED_PLAYERS = 0x801c4b64u;
static const uint32_t LOBBY_PREPARE_LAUNCH = 0x800f3c18u;
static const uint32_t DATABASE_IS_READY = 0x801e3854u;
static const uint32_t DATABASE_GET_SONG = 0x801e4aa0u;
static const uint32_t MANAGER_SET_SONG = 0x801c9644u;
static const uint32_t MANAGER_SET_MODE = 0x801c9950u;
static const uint32_t SONG_GET_DESCRIPTOR = 0x800a2338u;
static const uint32_t SPLASH_END_DISPLAYING = 0x8004e334u;

extern const char *jd_couch_requested_map(void) __attribute__((weak));
extern void jd_couch_library_ready(void) __attribute__((weak));
extern int jd_couch_loading(void) __attribute__((weak));
extern void jd_couch_gameplay_ready(void) __attribute__((weak));
extern void jd_couch_game_event(int recap) __attribute__((weak));

static const char *requested_map;
static uint32_t selected_entry;
static int map_seen, song_started, results_seen;

static int verified_title(void) {
  return title->title_low == SJOE41_TITLE_LOW && title->program_check == 0xd3bf2cfe5e138e37ull;
}

static void load_requested_map(void) {
  if (requested_map) return;
  requested_map = jd_couch_requested_map ? jd_couch_requested_map() : NULL;
  if (!requested_map) requested_map = getenv("JD2014_PROBE_MAP");
  if (requested_map && strlen(requested_map) >= 32) {
    fprintf(stderr, "probe: unsupported map key %s\n", requested_map);
    exit(2);
  }
}

static int launcher_boot(void) {
  return verified_title() && jd_couch_loading && jd_couch_loading();
}

void jd2014_probe_boot_wait(void) {
  if (!launcher_boot()) return;
  // The microphone, rating and static-logo screens each add a fixed display delay.
  if (cpu.lr == 0x80140fdcu || cpu.lr == 0x801413c4u || cpu.lr == 0x801416f4u)
    cpu.f[1] = 0;
}

void jd2014_probe_logo(void) {
  // This state is used only for the Ubisoft logo. Take its usual movie-ended branch.
  if (launcher_boot()) wr32(cpu.r[3] + 16, 1);
}

void jd2014_probe_gameplay(void) {
  if (map_seen && jd_couch_gameplay_ready) jd_couch_gameplay_ready();
}

static uint32_t guest_call(uint32_t address, uint32_t argument) {
  Cpu saved = cpu;
  cpu.r[3] = argument;
  dispatch(address);
  uint32_t result = cpu.r[3];
  cpu = saved;
  return result;
}

void jd2014_probe_system_splash(void) {
  if (!launcher_boot()) return;
  uint32_t splash = cpu.r[3];
  // Use the Wii splash screen's own teardown to restore graphics and input.
  if (rd32(splash + SPLASH_INITIALIZED) && rd32(splash + SPLASH_DISPLAYING) &&
      !rd32(splash + SPLASH_PAUSED))
    guest_call(SPLASH_END_DISPLAYING, splash);
}

static void verify_entry(uint32_t entry) {
  uint32_t descriptor = guest_call(SONG_GET_DESCRIPTOR, entry);
  uint32_t name = descriptor ? rd32(descriptor + DESCRIPTOR_MAP_NAME) : 0;
  size_t size = strlen(requested_map) + 1;
  for (size_t i = 0; name && i < size; i++) {
    if (rd8(name + i) != (unsigned char)requested_map[i]) name = 0;
  }
  if (!name) {
    fprintf(stderr, "probe: loader selected a different map than %s\n", requested_map);
    exit(2);
  }
}

int jd2014_probe_direct(void) {
  if (cpu.lr != DIRECT_LAUNCH_CALLER || !verified_title()) return 0;
  // The boot module prepares the menu before its normal startup transition. Wait until
  // that transition completes so the game's direct-launch branch can restart the menu.
  if (rd32(cpu.r[31] + ACTIVE_MODULE_OFFSET) != MODULE_MAIN_MENU) return 0;
  uint32_t manager = cpu.r[3], database = rd32(cpu.r[13] - DATABASE_SDA_OFFSET);
  if (!database || !guest_call(DATABASE_IS_READY, database)) return 0;
  if (jd_couch_library_ready) jd_couch_library_ready();
  load_requested_map();
  if (!requested_map) return 0;
  if (selected_entry) return 1;

  Cpu saved = cpu;
  // Leave the caller's locals alone. A temporary stack frame holds a borrowed String8.
  uint32_t stack = saved.r[1] - 96, string = stack + 32, data = stack + 64;
  size_t length = strlen(requested_map);
  wr32(stack, saved.r[1]);
  memcpy(host(data), requested_map, length + 1);
  wr32(string, data);
  wr32(string + 4, length + 1);
  wr32(string + 8, length);
  wr32(string + 12, 0);
  wr32(string + 16, 1);

  cpu.r[1] = stack;
  cpu.r[3] = database;
  cpu.r[4] = string;
  cpu.r[5] = SONG_MODE_CLASSIC;
  cpu.r[6] = SONG_VERSION_DISC;
  cpu.r[7] = UINT32_MAX; // Use the same lookup flags as PreLobby's confirmation.
  dispatch(DATABASE_GET_SONG);
  selected_entry = cpu.r[3];
  cpu = saved;
  if (!selected_entry) {
    fprintf(stderr, "probe: %s missing from the original song database\n", requested_map);
    exit(2);
  }
  verify_entry(selected_entry);

  cpu.r[1] = stack;
  cpu.r[3] = manager;
  cpu.r[4] = MODULE_GAMEPLAY_CLASSIC;
  dispatch(MANAGER_SET_MODE);
  cpu.r[3] = manager;
  cpu.r[4] = selected_entry;
  cpu.r[5] = 0;
  dispatch(MANAGER_SET_SONG);
  cpu = saved;
  fprintf(stderr, "probe: queued %s (classic, disc) at %.0f ms\n", requested_map, (double)time_base() / 60750);
  return 1;
}

void jd2014_probe_map(void) {
  if (!selected_entry || map_seen) return;
  verify_entry(cpu.r[4]);
  map_seen = 1;
  fprintf(stderr, "probe: original map loader requested %s at %.0f ms\n", requested_map, (double)time_base() / 60750);
}

static uint32_t ready_lobby;
static int launch_requested;

void jd2014_probe_lobby_ready(void) {
  // The lobby enables Start only after its gameplay connection reports ready.
  if (selected_entry && !ready_lobby) {
    ready_lobby = cpu.r[3];
    fprintf(stderr, "probe: original lobby enabled Start\n");
  }
}

void jd2014_probe_lobby_update(void) {
  if (!map_seen || !ready_lobby || cpu.r[3] != ready_lobby || launch_requested) return;
  uint32_t manager = rd32(cpu.r[13] - GAME_MANAGER_SDA_OFFSET);
  uint32_t connected = guest_call(MANAGER_CONNECTED_PLAYERS, manager);
  static int known_count = -1;
  if ((int)connected != known_count) {
    known_count = (int)connected;
    fprintf(stderr, "probe: original lobby sees %u connected players\n", connected);
  }
  if (connected != 1) return;

  // The normal Start action lets the game choose a coach for the connected player.
  launch_requested = 1;
  guest_call(LOBBY_PREPARE_LAUNCH, ready_lobby);
  fprintf(stderr, "probe: selected the original lobby Start action at %.0f ms\n", (double)time_base() / 60750);
}

void jd2014_probe_move_score(void) {
  if (!map_seen || results_seen) return;
  if (!song_started) {
    song_started = 1;
    jd2014_probe_gameplay();
    if (jd_couch_game_event) jd_couch_game_event(0);
    fprintf(stderr, "probe: original gameplay emitted its first move score at %.0f ms\n",
            (double)time_base() / 60750);
  }
  fprintf(stderr, "probe: original move score player %u value %.6f\n", cpu.r[4], cpu.f[1]);
}

void jd2014_probe_results(void) {
  if (map_seen && !results_seen) {
    results_seen = 1;
    if (jd_couch_game_event) jd_couch_game_event(1);
    fprintf(stderr, "probe: original recap entered at %.0f ms\n", (double)time_base() / 60750);
  }
}


// The Wii UbiArt releases use the same selection and lobby flow.
typedef struct {
  uint32_t title_low;
  uint64_t program_check;
  unsigned database_offset, active_module_offset, direct_flag_offset, classic_module;
  unsigned lobby_timer_offset;
  uint32_t direct_caller, database_ready, get_song, make_string, destroy_string;
  uint32_t set_mode, set_song, stop_lobby_timer, start_lobby;
  uint32_t lobby_howto_caller, splash_end, boot_wait_callers[4];
} UbiArtPort;

static const UbiArtPort ubiart_ports[] = {
  // Just Dance 2015 (SE3E41)
  {0x53453345, 0x6838caf6eb441eebull, 22160, 176, 412, 8, 22344,
   0x8023ae90, 0x80253160, 0x80254b2c, 0x800178ec, 0x8001784c,
   0x8022d39c, 0x8022d130, 0x80213560, 0x80126090,
   0, 0x8007b1f4, {0x80183bc0, 0x80183f7c, 0x801843e0}},
  // Just Dance 2016 (SJNE41)
  {0x534a4e45, 0x982a35fc4bc22d63ull, 20664, 152, 0, 7, 20840,
   0x80550994, 0x8056bd04, 0x8056d6d0, 0x8000a448, 0x8000a3a8,
   0x80546a04, 0x80546798, 0x80536950, 0x80447668,
   0x80519960, 0x80630684, {0x8049dbcc, 0x8049df7c, 0x8049e2c8, 0x8049e72c}},
  // Just Dance 2017 (SZ7E41)
  {0x535a3745, 0x2a329d91781d87a0ull, 20640, 152, 0, 7, 20816,
   0x80551d34, 0x8056d4c8, 0x8056ee94, 0x8000a448, 0x8000a3a8,
   0x80547da4, 0x80547b38, 0x80537c58, 0x804481b0,
   0x8051adc0, 0x80632bdc, {0x8049eb8c, 0x8049ef3c, 0x8049f288, 0x8049f6ec}},
  // Just Dance 2018 (SE8E41)
  {0x53453845, 0xe8cdcc699a88d248ull, 20656, 152, 0, 7, 20832,
   0x80550c88, 0x8056cac4, 0x8056e490, 0x8000a4e8, 0x8000a448,
   0x80546cec, 0x80546a80, 0x80536a14, 0x804489d0,
   0x80519ba4, 0x80630c3c, {0x8049debc, 0x8049e278, 0x8049e6dc}},
  // Just Dance 2019 (S5EE41)
  {0x53354545, 0x351e518638c91d83ull, 21480, 152, 0, 7, 21656,
   0x80541458, 0x8055cfbc, 0x8055e988, 0x8000a290, 0x8000a1f0,
   0x80536848, 0x805365dc, 0x805268cc, 0x8043e834,
   0x80509a7c, 0x80614304, {0x80492dec, 0x804931a8, 0x8049360c}},
};
static const UbiArtPort *active_port;

enum { LOBBY_PHASE_OFFSET = 52, LOBBY_ENTERED = 1, LOBBY_ACCEPTS_LAUNCH = 2 };

static const UbiArtPort *ubiart_port(void) {
  for (size_t i = 0; i < sizeof ubiart_ports / sizeof ubiart_ports[0]; i++) {
    if (title->title_low == ubiart_ports[i].title_low && title->program_check == ubiart_ports[i].program_check)
      return &ubiart_ports[i];
  }
  return NULL;
}

static const UbiArtPort *ubiart_boot(void) {
  return jd_couch_loading && jd_couch_loading() ? ubiart_port() : NULL;
}

void jd2015_probe_boot_wait(void) {
  const UbiArtPort *port = ubiart_boot();
  if (!port) return;
  // These callers are the fixed-duration boot screens.
  for (size_t i = 0; i < sizeof port->boot_wait_callers / sizeof port->boot_wait_callers[0]; i++)
    if (cpu.lr == port->boot_wait_callers[i]) cpu.f[1] = 0;
}

void jd2015_probe_logo(void) {
  if (ubiart_boot()) wr32(cpu.r[3] + 20, 1);
}

void jd2015_probe_splash(void) {
  const UbiArtPort *port = ubiart_boot();
  if (!port) return;
  uint32_t splash = cpu.r[3];
  if (rd32(splash + SPLASH_INITIALIZED) && rd32(splash + SPLASH_DISPLAYING) &&
      !rd32(splash + SPLASH_PAUSED))
    guest_call(port->splash_end, splash);
}

int jd2015_probe_direct(void) {
  const UbiArtPort *port = ubiart_port();
  if (!port || cpu.lr != port->direct_caller ||
      rd32(cpu.r[31] + port->active_module_offset) != MODULE_MAIN_MENU) return 0;
  uint32_t manager = cpu.r[3];
  uint32_t database = rd32(cpu.r[13] - port->database_offset);
  if (!database || !guest_call(port->database_ready, database)) return 0;
  active_port = port;
  if (jd_couch_library_ready) jd_couch_library_ready();
  load_requested_map();
  if (!requested_map) return 0;
  if (selected_entry) return 1;

  Cpu saved = cpu;
  uint32_t stack = saved.r[1] - 96, string = stack + 32, data = stack + 64;
  size_t length = strlen(requested_map);
  wr32(stack, saved.r[1]);
  memcpy(host(data), requested_map, length + 1);
  cpu.r[1] = stack;
  cpu.r[3] = string;
  cpu.r[4] = data;
  dispatch(port->make_string);  // String8(const char *)
  cpu.r[3] = database;
  cpu.r[4] = string;
  cpu.r[5] = SONG_MODE_CLASSIC;
  cpu.r[6] = SONG_VERSION_DISC;
  cpu.r[7] = UINT32_MAX;
  dispatch(port->get_song);
  selected_entry = cpu.r[3];
  cpu.r[3] = string;
  cpu.r[4] = UINT32_MAX;
  dispatch(port->destroy_string);  // ~String8()
  if (!selected_entry) {
    fprintf(stderr, "probe: %s missing from the original song database\n", requested_map);
    exit(2);
  }
  cpu.r[3] = manager;
  cpu.r[4] = port->classic_module;
  dispatch(port->set_mode);
  cpu.r[3] = manager;
  cpu.r[4] = selected_entry;
  cpu.r[5] = stack + 24;
  wr32(cpu.r[5], 0);
  dispatch(port->set_song);
  cpu = saved;
  if (port->direct_flag_offset) wr32(manager + port->direct_flag_offset, 1);
  fprintf(stderr, "probe: queued %s (classic, disc)\n", requested_map);
  return 1;
}

void jd2015_probe_map(void) {
  if (!selected_entry || map_seen) return;
  if (cpu.r[4] != selected_entry) {
    fprintf(stderr, "probe: loader selected a different map than %s\n", requested_map);
    exit(2);
  }
  map_seen = 1;
  fprintf(stderr, "probe: original map loader requested %s\n", requested_map);
}

static int lobby_armed;

void jd2015_probe_lobby_phase(void) {
  if (!map_seen || !ready_lobby || lobby_armed) return;
  uint32_t phase = cpu.r[3] + LOBBY_PHASE_OFFSET;
  // The hidden lobby has already entered. Carry the picker confirmation into the
  // phase that accepts LaunchMap, then let the original lobby handle the transition.
  uint32_t state = rd32(phase);
  if (state == LOBBY_ENTERED) wr32(phase, LOBBY_ACCEPTS_LAUNCH);
  else if (state != LOBBY_ACCEPTS_LAUNCH) return;
  lobby_armed = 1;
}

void jd2015_probe_lobby_update(void) {
  if (!lobby_armed || cpu.r[3] != ready_lobby || launch_requested) return;
  launch_requested = 1;
  guest_call(active_port->stop_lobby_timer, rd32(cpu.r[13] - active_port->lobby_timer_offset));
  guest_call(active_port->start_lobby, ready_lobby);
  fprintf(stderr, "probe: selected the original lobby Start action\n");
}

int jd2015_probe_controller_prompt(void) {
  // The hidden Wii Remote grip reminder normally waits for the same A press
  // the user has already used to choose this song in the launcher.
  return selected_entry && !map_seen && active_port && jd_couch_loading && jd_couch_loading();
}

int jd2015_probe_skip_lobby_howto(void) {
  // The launcher's A press confirms the grip reminder. Tell the lobby entry
  // state that no popup opened, so it can finish its normal setup immediately.
  return jd2015_probe_controller_prompt() && cpu.lr == active_port->lobby_howto_caller;
}


// Just Dance 3 revision 1 ships these scripts as an optional native RSO.
enum {
  SJDE41_TITLE_LOW = 0x534a4445,
  JD3_COMPILED_SCRIPTS_SDA_OFFSET = 27936,
  JD3_FIND_MAP = 0x1773fc,
  JD3_LAUNCH_MAP = 0x1ba6e8,
  JD3_LOBBY_READY = 0x0bcce0,
  JD3_LOBBY_CONTINUE = 0x0bce64,
  JD3_LOBBY_END_MENU_CHANGE = 0x0bcb20,
};

static uint32_t jd3_code, jd3_key, jd3_lobby;
static int jd3_map_confirmed, jd3_continued, jd3_lobby_complete, jd3_scoring;

void jd3_probe_enable_compiled_scripts(void) {
  if (title->title_low != SJDE41_TITLE_LOW || title->program_check != 0x3f798cb17a63269dull)
    return;
  requested_map = getenv("JD3_PROBE_MAP");
  if (requested_map) wr32(cpu.r[13] - JD3_COMPILED_SCRIPTS_SDA_OFFSET, 1);
}

void jd3_probe_title_ready(uint32_t page, uint32_t code) {
  if (!requested_map || jd3_key) return;
  jd3_code = code;

  char *end;
  unsigned long key = strtoul(requested_map, &end, 16);
  if (!key || key > UINT32_MAX || *end) exit(2);
  Cpu saved = cpu;
  cpu.r[5] = key;
  dispatch(jd3_code + JD3_FIND_MAP);
  int found = (int32_t)cpu.r[3] >= 0;
  cpu = saved;
  if (!found) {
    fprintf(stderr, "probe: %s missing from the original JD3 song database\n", requested_map);
    exit(2);
  }

  // LaunchMap reads the first word of its stock argument as the map key.
  // The title page and menu actions share the same script context.
  jd3_key = key;
  uint32_t stock = saved.r[1] - 160;
  wr32(stock, jd3_key);
  wr32(stock - 32, saved.r[1]);
  cpu.r[1] = stock - 32;
  cpu.r[3] = page;
  cpu.r[4] = stock;
  dispatch(jd3_code + JD3_LAUNCH_MAP);
  cpu = saved;
}

void jd3_probe_map(void) {
  if (!jd3_key || jd3_map_confirmed) return;
  if (cpu.r[5] != jd3_key) {
    fprintf(stderr, "probe: JD3 loader selected a different map than %s\n", requested_map);
    exit(2);
  }
  jd3_map_confirmed = 1;
  fprintf(stderr, "probe: original map loader requested %s\n", requested_map);
}

void jd3_probe_lobby_entered(void) {
  if (jd3_map_confirmed) jd3_lobby = cpu.r[3];
}

void jd3_probe_lobby_update(void) {
  if (!jd3_lobby || cpu.r[3] != jd3_lobby || jd3_continued) return;
  if (!guest_call(jd3_code + JD3_LOBBY_READY, jd3_lobby)) return;
  jd3_continued = guest_call(jd3_code + JD3_LOBBY_CONTINUE, jd3_lobby);
  if (jd3_continued) fprintf(stderr, "probe: selected the original JD3 lobby Continue action\n");
}

int jd3_probe_first_lobby_completion(void) {
  if (!jd3_continued || cpu.r[3] != jd3_lobby) return 1;
  if (jd3_lobby_complete) return 0;
  jd3_lobby_complete = 1;
  return 1;
}

void jd3_probe_hud_entered(void) {
  if (!jd3_continued || jd3_lobby_complete) return;
  // The direct path has prepared the dance and entered the HUD. Complete the
  // lobby transition through its original callback, which starts phase six.
  guest_call(jd3_code + JD3_LOBBY_END_MENU_CHANGE, jd3_lobby);
}

void jd3_probe_game_started(void) {
  if (!jd3_lobby_complete) return;
  fprintf(stderr, "probe: original JD3 gameplay started\n");
}

void jd3_probe_score(void) {
  if (!jd3_lobby_complete || jd3_scoring) return;
  jd3_scoring = 1;
  fprintf(stderr, "probe: original JD3 gameplay emitted its first move score %.6f\n", cpu.f[1]);
}

void jd3_probe_recap(void) {
  if (!jd3_scoring) return;
  fprintf(stderr, "probe: original JD3 scoreboard entered\n");
}

enum {
  SDNE41_TITLE_LOW = 0x53444e45,
  JD1_INPUT_POLL = 0x801f5734,
  JD1_TICKS_PER_MS = 60750,
  JD1_CONFIRM_PERIOD_MS = 1300,
  JD1_CONFIRM_HELD_MS = 400,
  JD1_LOBBY_SETTLE_MS = 3000,
  JD1_KPAD_HOLD = 0,
  JD1_KPAD_TRIGGER = 4,
  JD1_KPAD_RELEASE = 8,
  JD1_KPAD_POINTER = 0x20,
  JD1_KPAD_POINTER_VALID = 0x5e,
  JD1_POINTER_SEEN = 2,
};

static int jd1_active, jd1_saved, jd1_movie_seen, jd1_score_seen, jd1_recap_seen;
static unsigned jd1_key, jd1_step;
static uint64_t jd1_lobby_ready;
static char jd1_movie[48];

int jd1_probe_direct(void) {
  if (title->title_low != SDNE41_TITLE_LOW || title->program_check != 0xf3890e503b9a273aull)
    return 0;
  if (jd1_active) return 1;
  const char *map = jd_couch_requested_map ? jd_couch_requested_map() : NULL;
  if (!map) map = getenv("JD1_PROBE_MAP");
  if (!map) return 0;
  char *end;
  unsigned long key = strtoul(map, &end, 10);
  if (end == map || *end || key > UINT32_MAX) exit(2);
  jd1_key = key;
  jd1_active = 1;
  return 1;
}

int jd1_probe_skip_song(void) {
  if (!jd1_active) return 0;
  if (cpu.r[5] != jd1_key) return 1;
  snprintf(jd1_movie, sizeof jd1_movie, "%s", (const char *)host(cpu.r[9]));
  fprintf(stderr, "probe: original Just Dance selected %s (%u)\n", jd1_movie, jd1_key);
  return 0;
}

int jd1_probe_menu_choice(uint32_t code) {
  // FLA_Menu_GetSelection stores each choice in the calling script's output.
  static const struct { uint32_t caller, choice; } menu[] = {
    {0x64cb4, 1},  // Quick Play
    {0x12f4b0, 6}, // Regular
    {0x13dad0, 0}, // One round
    {0x14bfb4, 0}, // Automatic song pick
  };
  if (!jd1_active || jd1_step == sizeof menu / sizeof *menu ||
      cpu.lr - code != menu[jd1_step].caller) return 0;
  wr32(cpu.r[4], menu[jd1_step++].choice);
  return 1;
}

void jd1_probe_input(uint32_t caller, uint32_t channel, uint32_t count, uint32_t status) {
  if (!jd1_active || caller != JD1_INPUT_POLL || channel || !count || jd1_movie_seen) return;
  if (jd1_step && !jd1_lobby_ready) return;
  uint64_t elapsed = time_base() / JD1_TICKS_PER_MS;
  int hold = elapsed % JD1_CONFIRM_PERIOD_MS < JD1_CONFIRM_HELD_MS;
  if (jd1_lobby_ready) {
    // The stock Full version button runs its Flash transition on a pointer click.
    static const union { float f; uint32_t bits; } x = {.f = 0.36f}, y = {.f = 0.34f};
    wr32(status + JD1_KPAD_POINTER, x.bits);
    wr32(status + JD1_KPAD_POINTER + 4, y.bits);
    wr8(status + JD1_KPAD_POINTER_VALID, JD1_POINTER_SEEN);
    hold = hold && elapsed - jd1_lobby_ready >= JD1_LOBBY_SETTLE_MS;
  }
  static int previous;
  wr32(status + JD1_KPAD_HOLD, (rd32(status + JD1_KPAD_HOLD) & ~BUTTON_A) | (hold ? BUTTON_A : 0));
  wr32(status + JD1_KPAD_TRIGGER, (rd32(status + JD1_KPAD_TRIGGER) & ~BUTTON_A) |
       (hold && !previous ? BUTTON_A : 0));
  wr32(status + JD1_KPAD_RELEASE, (rd32(status + JD1_KPAD_RELEASE) & ~BUTTON_A) |
       (!hold && previous ? BUTTON_A : 0));
  previous = hold;
}

void jd1_probe_video_open(void) {
  if (!jd1_lobby_ready || jd1_movie_seen) return;
  const char *path = host(cpu.r[3]);
  if (strncmp(path, "/videos/", 8)) return;
  char wide[sizeof jd1_movie + 16], standard[sizeof wide];
  snprintf(wide, sizeof wide, "/videos/%s169.bik", jd1_movie);
  snprintf(standard, sizeof standard, "/videos/%s.bik", jd1_movie);
  if (strcasecmp(path, wide) && strcasecmp(path, standard)) {
    fprintf(stderr, "probe: expected %s, original Just Dance opened %s\n", wide, path);
    exit(2);
  }
  jd1_movie_seen = 1;
  fprintf(stderr, "probe: original Just Dance opened %s\n", path);
}

void jd1_probe_video_frame(void) {
  if (jd1_movie_seen && jd_couch_gameplay_ready) jd_couch_gameplay_ready();
}

void jd1_probe_flash_value(uint32_t name_address) {
  if (!jd1_active || !name_address) return;
  const char *name = host(name_address);
  if (!jd1_step && !jd1_saved && !strcmp(name, "_root.bouton1_pressed")) {
    jd1_saved = 1;
    cpu.r[3] = 1;
  } else if (jd1_step == 4 && !jd1_lobby_ready && !strcmp(name, "_root.PlayLong")) {
    jd1_lobby_ready = time_base() / JD1_TICKS_PER_MS;
  } else if (jd1_movie_seen && !jd1_score_seen && !strcmp(name, "_root.Score_Current")) {
    jd1_score_seen = 1;
    if (jd_couch_game_event) jd_couch_game_event(0);
  } else if (jd1_movie_seen && !jd1_recap_seen && cpu.r[3] == 1 && !strcmp(name, "_root.DisplayScoreReward")) {
    jd1_recap_seen = 1;
    if (jd_couch_game_event) jd_couch_game_event(1);
  }
}
