// Guest entry points and layouts from the SJOE41 revision 0 debug ELF.
#include <stdio.h>
#include <stdlib.h>

#include "cpu.h"
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


// Equivalent entry points and layouts in the verified SE3E41 revision 0 DOL.
enum {
  SE3E41_TITLE_LOW = 0x53453345,
  JD2015_DATABASE_SDA_OFFSET = 22160,
  JD2015_ACTIVE_MODULE_OFFSET = 176,
  JD2015_DIRECT_FLAG_OFFSET = 412,
  JD2015_CLASSIC_MODULE = 8,
  JD2015_LOBBY_TIMER_SDA_OFFSET = 22344,
  JD2015_LOBBY_PHASE_OFFSET = 52,
  JD2015_LOBBY_ENTERED = 1,
  JD2015_LOBBY_ACCEPTS_LAUNCH = 2,
};
static const uint32_t JD2015_DIRECT_CALLER = 0x8023ae90u;
static const uint32_t JD2015_DATABASE_READY = 0x80253160u;
static const uint32_t JD2015_DATABASE_GET_SONG = 0x80254b2cu;
static const uint32_t JD2015_SET_MODE = 0x8022d39cu;
static const uint32_t JD2015_SET_SONG = 0x8022d130u;
static const uint32_t JD2015_LOBBY_STOP_TIMER = 0x80213560u;
static const uint32_t JD2015_LOBBY_START = 0x80126090u;
static const uint32_t JD2015_SPLASH_END = 0x8007b1f4u;

static int verified_2015(void) {
  return title->title_low == SE3E41_TITLE_LOW && title->program_check == 0x6838caf6eb441eebull;
}

static int launcher_boot_2015(void) {
  return verified_2015() && jd_couch_loading && jd_couch_loading();
}

void jd2015_probe_boot_wait(void) {
  if (!launcher_boot_2015()) return;
  // Only the boot screen callers use these fixed display delays.
  if (cpu.lr == 0x80183bc0u || cpu.lr == 0x80183f7cu || cpu.lr == 0x801843e0u)
    cpu.f[1] = 0;
}

void jd2015_probe_logo(void) {
  if (launcher_boot_2015()) wr32(cpu.r[3] + 20, 1);
}

void jd2015_probe_splash(void) {
  if (!launcher_boot_2015()) return;
  uint32_t splash = cpu.r[3];
  if (rd32(splash + SPLASH_INITIALIZED) && rd32(splash + SPLASH_DISPLAYING) &&
      !rd32(splash + SPLASH_PAUSED))
    guest_call(JD2015_SPLASH_END, splash);
}

int jd2015_probe_direct(void) {
  if (!verified_2015() || cpu.lr != JD2015_DIRECT_CALLER ||
      rd32(cpu.r[31] + JD2015_ACTIVE_MODULE_OFFSET) != MODULE_MAIN_MENU) return 0;
  uint32_t manager = cpu.r[3];
  uint32_t database = rd32(cpu.r[13] - JD2015_DATABASE_SDA_OFFSET);
  if (!database || !guest_call(JD2015_DATABASE_READY, database)) return 0;
  if (jd_couch_library_ready) jd_couch_library_ready();
  load_requested_map();
  if (!requested_map) return 0;
  if (selected_entry) return 1;

  Cpu saved = cpu;
  uint32_t stack = saved.r[1] - 96, string = stack + 32, data = stack + 64;
  size_t length = strlen(requested_map);
  wr32(stack, saved.r[1]);
  wr32(stack, saved.r[1]);
  memcpy(host(data), requested_map, length + 1);
  cpu.r[1] = stack;
  cpu.r[3] = string;
  cpu.r[4] = data;
  dispatch(0x800178ecu);  // String8(const char *)
  cpu.r[3] = database;
  cpu.r[4] = string;
  cpu.r[5] = SONG_MODE_CLASSIC;
  cpu.r[6] = SONG_VERSION_DISC;
  cpu.r[7] = UINT32_MAX;
  dispatch(JD2015_DATABASE_GET_SONG);
  selected_entry = cpu.r[3];
  cpu.r[3] = string;
  cpu.r[4] = UINT32_MAX;
  dispatch(0x8001784cu);  // ~String8()
  if (!selected_entry) {
    fprintf(stderr, "probe: %s missing from the original song database\n", requested_map);
    exit(2);
  }
  cpu.r[3] = manager;
  cpu.r[4] = JD2015_CLASSIC_MODULE;
  dispatch(JD2015_SET_MODE);
  cpu.r[3] = manager;
  cpu.r[4] = selected_entry;
  cpu.r[5] = stack + 24;
  wr32(cpu.r[5], 0);
  dispatch(JD2015_SET_SONG);
  cpu = saved;
  wr32(manager + JD2015_DIRECT_FLAG_OFFSET, 1);
  fprintf(stderr, "probe: queued %s (2015 classic, disc)\n", requested_map);
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
  uint32_t phase = cpu.r[3] + JD2015_LOBBY_PHASE_OFFSET;
  // The hidden lobby has already entered. Carry the picker confirmation into the
  // phase that accepts LaunchMap, then let the original lobby handle the transition.
  if (rd32(phase) != JD2015_LOBBY_ENTERED) return;
  wr32(phase, JD2015_LOBBY_ACCEPTS_LAUNCH);
  lobby_armed = 1;
}

void jd2015_probe_lobby_update(void) {
  if (!lobby_armed || cpu.r[3] != ready_lobby || launch_requested) return;
  launch_requested = 1;
  guest_call(JD2015_LOBBY_STOP_TIMER, rd32(cpu.r[13] - JD2015_LOBBY_TIMER_SDA_OFFSET));
  guest_call(JD2015_LOBBY_START, ready_lobby);
  fprintf(stderr, "probe: selected the original 2015 lobby Start action\n");
}

int jd2015_probe_controller_prompt(void) {
  // The hidden Wii Remote grip reminder normally waits for the same A press
  // the user has already used to choose this song in the launcher.
  return selected_entry && !map_seen && launcher_boot_2015();
}
