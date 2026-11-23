#include <SDL3/SDL_timer.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "audio.h"
#include "input.h"
#include "paths.h"
#include "remote_link.h"
#include "session.h"
#include "song_grid.h"

int jd_runtime_main(int count, char **arguments);

int session_send_player(int socket, enum SessionMessageType type, unsigned player,
                        const void *data, unsigned size) {
  SessionMessage message = {.type = type, .size = size, .player = player};
  if (player >= REMOTE_PLAYERS || size > sizeof message.data) return 0;
  if (size) memcpy(message.data, data, size);
  size_t bytes = offsetof(SessionMessage, data) + size;
  return send(socket, &message, bytes, MSG_DONTWAIT | MSG_NOSIGNAL) == (ssize_t)bytes;
}

int session_send(int socket, enum SessionMessageType type, const void *data, unsigned size) {
  return session_send_player(socket, type, 0, data, size);
}

int session_read(int socket, SessionMessage *message, int flags) {
  ssize_t bytes = recv(socket, message, sizeof *message, flags | MSG_TRUNC);
  if (bytes <= 0) return (int)bytes;
  if (bytes < (ssize_t)offsetof(SessionMessage, data) || message->size > sizeof message->data ||
      message->player >= REMOTE_PLAYERS || bytes != (ssize_t)(offsetof(SessionMessage, data) + message->size)) {
    errno = EPROTO;
    return -1;
  }
  return 1;
}

static int bridge_socket = -1;
static int test_input;
static unsigned boot_rate;
enum { LEGACY_JOIN_HOLD_MS = 330 };
static int join_with_a;
static atomic_uint_fast64_t join_until;
enum { GAME_WINDOW_NORMAL, GAME_WINDOW_HIDDEN, GAME_WINDOW_READY, GAME_WINDOW_SHOWN };
static atomic_int game_window;
static atomic_int requested_song, boot_parked;
static pthread_mutex_t selection_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t selection_ready = PTHREAD_COND_INITIALIZER;
static int bridge_closed;
static int library_ready;
static const Edition *edition;
static const Song *add_on;

static const Song *selected_song(void) {
  if (add_on) return add_on;
  int selected = atomic_load(&requested_song);
  return selected ? &edition->songs[selected - 1] : NULL;
}

const char *jd_couch_requested_map(void) {
  const Song *song = selected_song();
  return song ? song->map : NULL;
}

#define LEVEL_PEAK 27570.0f  // -1.5 dBFS at the native sample rate.
#define RELEASE_SECONDS 0.12f

// Keep one 3 ms block in reserve, so the gain can fall smoothly before a
// peak. Both ends of the ramp are safe for every sample in the delayed block.
static void level_block(int16_t *samples, float target) {
  static int16_t delayed[AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS];
  static float gain, delayed_limit;
  int peak = 0;
  for (uint32_t i = 0; i < AUDIO_FRAME_SAMPLES * AUDIO_CHANNELS; i++) {
    int magnitude = abs((int)samples[i]);
    if (magnitude > peak) peak = magnitude;
  }
  float limit = peak ? fminf(target, LEVEL_PEAK / peak) : target;
  if (delayed_limit == 0) gain = delayed_limit = target;
  float released = gain + (target - gain) * (AUDIO_FRAME_SAMPLES / (AUDIO_SAMPLE_RATE * RELEASE_SECONDS));
  float end = fminf(released, fminf(delayed_limit, limit));
  float step = (end - gain) / AUDIO_FRAME_SAMPLES;
  for (uint32_t i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
    float level = gain + step * i;
    for (uint32_t channel = 0; channel < AUDIO_CHANNELS; channel++) {
      uint32_t at = i * AUDIO_CHANNELS + channel;
      int16_t input = samples[at];
      samples[at] = (int16_t)(delayed[at] * level);
      delayed[at] = input;
    }
  }
  gain = end;
  delayed_limit = limit;
}

void jd_couch_level_audio(int16_t *samples, uint32_t frames) {
  const Song *song = selected_song();
  if (!song || song->gameplay_gain <= 0 || frames % AUDIO_FRAME_SAMPLES) return;
  for (uint32_t offset = 0; offset < frames; offset += AUDIO_FRAME_SAMPLES)
    level_block(samples + offset * AUDIO_CHANNELS, song->gameplay_gain);
}

unsigned jd_couch_dlc_index(void) {
  return add_on ? add_on->content_index : 0;
}

void jd_couch_library_ready(void) {
  if (bridge_socket >= 0 && !library_ready) {
    library_ready = 1;
    session_send(bridge_socket, LIBRARY_READY, NULL, 0);
  }
}

int jd_couch_waiting_for_song(void) {
  return bridge_socket >= 0 && !add_on && !atomic_load(&requested_song);
}

const char *jd_couch_wait_for_map(void) {
  if (bridge_socket < 0) return NULL;
  if (add_on) return add_on->map;
  pthread_mutex_lock(&selection_lock);
  atomic_store(&boot_parked, !atomic_load(&requested_song));
  jd_couch_library_ready();
  while (!atomic_load(&requested_song) && !bridge_closed)
    pthread_cond_wait(&selection_ready, &selection_lock);
  atomic_store(&boot_parked, 0);
  pthread_mutex_unlock(&selection_lock);
  return jd_couch_requested_map();
}

unsigned jd_couch_boot_rate(void) {
  return boot_rate;
}

int jd_couch_boot_parked(void) {
  return atomic_load(&boot_parked);
}

int jd_couch_reveal_ready(void) {
  return atomic_load(&game_window) == GAME_WINDOW_READY;
}

int jd_couch_loading(void) {
  int state = atomic_load(&game_window);
  return state == GAME_WINDOW_HIDDEN || state == GAME_WINDOW_READY;
}

void jd_couch_gameplay_ready(void) {
  int hidden = GAME_WINDOW_HIDDEN;
  atomic_compare_exchange_strong(&game_window, &hidden, GAME_WINDOW_READY);
}

void jd_couch_window_shown(void) {
  // The LyN games use A to enroll additional dancers when play begins.
  if (join_with_a) atomic_store(&join_until, SDL_GetTicks() + LEGACY_JOIN_HOLD_MS);
  atomic_store(&game_window, GAME_WINDOW_SHOWN);
  session_send(bridge_socket, GAME_VISIBLE, NULL, 0);
}

void jd_couch_game_event(int recap) {
  if (bridge_socket >= 0) session_send(bridge_socket, recap ? RECAP_ENTERED : DANCE_STARTED, NULL, 0);
}

int jd_couch_test_key(int escape, int enter) {
  if (!test_input || bridge_socket < 0 || (!escape && !enter)) return 0;
  uint16_t buttons = escape ? BUTTON_HOME : BUTTON_A;
  session_send(bridge_socket, TEST_BUTTONS, &buttons, sizeof buttons);
  return 1;
}

typedef struct {
  int link, connected;
  uint8_t address[REMOTE_ADDRESS_BYTES];
} BridgeRemote;

static BridgeRemote bridge_remotes[REMOTE_PLAYERS];

static int write_remote(void *device, const uint8_t *report, uint32_t bytes) {
  unsigned player = (BridgeRemote *)device - bridge_remotes;
  return session_send_player(bridge_socket, REMOTE_OUTPUT, player, report, bytes) ? (int)bytes : -1;
}

static int keep_remote_open(void *unused, int guest_power_off) {
  (void)unused;
  (void)guest_power_off;
  return 0;
}

static void *read_remote(void *unused) {
  (void)unused;
  static const LinkOps operations = {write_remote, keep_remote_open};
  for (unsigned i = 0; i < REMOTE_PLAYERS; i++) bridge_remotes[i].link = -1;
  struct pollfd socket = {.fd = bridge_socket, .events = POLLIN};
  for (;;) {
    int retry = 0;
    for (unsigned i = 0; i < REMOTE_PLAYERS; i++) {
      BridgeRemote *remote = &bridge_remotes[i];
      if (remote->connected && remote->link < 0) {
        remote->link = link_attach_remote(i, remote->address, &operations, remote);
        retry |= remote->link < 0;
      }
    }
    // A reconnect can arrive before the guest consumes the preceding disconnect.
    int ready = poll(&socket, 1, retry ? 16 : -1);
    if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
    SessionMessage message;
    if (ready < 0 || session_read(bridge_socket, &message, 0) <= 0) break;
    BridgeRemote *remote = &bridge_remotes[message.player];
    switch (message.type) {
      case SELECT_SONG:
        if (message.size == 1 && !add_on && edition && message.data[0] < edition->count) {
          pthread_mutex_lock(&selection_lock);
          atomic_store(&requested_song, message.data[0] + 1);
          pthread_cond_signal(&selection_ready);
          pthread_mutex_unlock(&selection_lock);
        }
        break;
      case REMOTE_CONNECTED:
        if (message.size != sizeof remote->address) break;
        if (remote->connected && !memcmp(remote->address, message.data, sizeof remote->address)) break;
        if (remote->link >= 0) link_lost(remote->link);
        remote->link = -1;
        remote->connected = 1;
        memcpy(remote->address, message.data, sizeof remote->address);
        break;
      case REMOTE_DISCONNECTED:
        if (remote->link >= 0) link_lost(remote->link);
        remote->link = -1;
        remote->connected = 0;
        break;
      case REMOTE_INPUT: {
        if (remote->link < 0 || !message.size) break;
        uint64_t deadline = atomic_load(&join_until);
        if (deadline && message.player && message.size >= 3 &&
            message.data[0] >= 0x30 && message.data[0] <= 0x3f && SDL_GetTicks() < deadline)
          message.data[2] |= BUTTON_A >> 8;
        link_input(remote->link, message.data, message.size);
        break;
      }
      default:
        break;
    }
  }
  for (unsigned i = 0; i < REMOTE_PLAYERS; i++)
    if (bridge_remotes[i].link >= 0) link_lost(bridge_remotes[i].link);
  pthread_mutex_lock(&selection_lock);
  bridge_closed = 1;
  pthread_cond_signal(&selection_ready);
  pthread_mutex_unlock(&selection_lock);
  return NULL;
}

static void start_bridge_reader(void) {
  pthread_t thread;
  if (pthread_create(&thread, NULL, read_remote, NULL) != 0) {
    fprintf(stderr, "couch: cannot start the remote report bridge\n");
    exit(10);
  }
  pthread_detach(thread);
}

int jd_couch_bridge_start(void) {
  if (test_input || bridge_socket < 0) return 0;
  start_bridge_reader();
  return 1;
}

static int check_header(const char *root, const char *disc, const char *relative, const void *expected, size_t size) {
  char path[PATH_MAX];
  if (snprintf(path, sizeof path, "%s/discs/%s/%s", root, disc, relative) >= (int)sizeof path) return 0;
  FILE *file = fopen(path, "rb");
  if (!file) return 0;
  uint8_t actual[8];
  int matches = fread(actual, 1, size, file) == size && !memcmp(actual, expected, size);
  fclose(file);
  return matches;
}

int run_worker(char *program, const char *disc, const char *map, int socket, int simulated) {
  char cache[PATH_MAX], state[PATH_MAX], sys[PATH_MAX], saves[PATH_MAX], nand[PATH_MAX], builtin[24];
  if (!strcmp(disc, "DLC")) {
    const Edition *page = &editions[edition_count - 1];
    char *end;
    long row = map ? strtol(map, &end, 10) : -1;
    if (page->disc || !map || !*map || *end || row < 0 || row >= page->count) return 10;
    add_on = &page->songs[row];
    disc = add_on->disc;
    map = add_on->map;
  }
  edition = edition_for_disc(disc);
  if (!edition && strcmp(disc, "SJDE41")) return 10;
  if (!root_path(cache, sizeof cache, "XDG_CACHE_HOME", ".cache") ||
      !root_path(state, sizeof state, "XDG_DATA_HOME", ".local/share") ||
      snprintf(sys, sizeof sys, "%s/discs/%s/sys", cache, disc) >= (int)sizeof sys ||
      snprintf(saves, sizeof saves, "%s/%s", state, disc) >= (int)sizeof saves) return 10;
  if (add_on && (snprintf(nand, sizeof nand, "%s/dlc/%s/mounts/%u", cache,
                          add_on->pack, add_on->content_index) >= (int)sizeof nand ||
                 access(nand, R_OK | X_OK))) return 10;
  uint8_t header[8] = {0}, usa[] = {0, 0, 0, 1};
  memcpy(header, disc, 6);
  header[7] = !strcmp(disc, "SJDE41");
  if (!check_header(cache, disc, "disc/header.bin", header, sizeof header) ||
      !check_header(cache, disc, "disc/region.bin", usa, sizeof usa)) {
    fprintf(stderr, "couch: expected the matching USA revision of %s in %s\n", disc, cache);
    return 10;
  }
  if (!directory_tree(saves) || chdir(state) != 0) {
    perror("couch saves");
    return 10;
  }
  bridge_socket = socket;
  test_input = simulated;
  snprintf(builtin, sizeof builtin, "builtin:%s", disc);
  setenv("WII_RECOMP_NATIVE", builtin, 1);
  setenv("WII_RECOMP_NATIVE_ONLY", "1", 1);
  setenv("WII_RECOMP_SAVES", saves, 1);
  setenv("WII_RECOMP_REMOTES", simulated ? "loopback" : "real", 1);
  setenv("WII_RECOMP_ATTACHMENT", "none", 0);
  setenv("WII_RECOMP_SHOP_SERVER", "off", 1);
  if (socket >= 0) {
    const char *trace = getenv("JUST_DANCE_GAME_TRACE");
    if (!trace || strcmp(trace, "1")) {
      char log[PATH_MAX];
      int length = snprintf(log, sizeof log, "%s/game-%ld.log", cache, (long)getppid());
      if (length >= 0 && length < (int)sizeof log) {
        int fd = open(log, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
          dup2(fd, STDOUT_FILENO);
          dup2(fd, STDERR_FILENO);
          close(fd);
        }
      }
    }
    setenv("WII_RECOMP_FULLSCREEN", "1", 1);
    atomic_store(&game_window, GAME_WINDOW_HIDDEN);
    if (simulated) start_bridge_reader();
  }
  setenv("WII_RECOMP_REMOTE_SLOTS", "remotes.txt", 0);
  setenv("WII_RECOMP_CONTROLLERS", "controllers.ini", 0);
  const char *map_variable = NULL;
  if (!strcmp(disc, "SDNE41")) map_variable = "JD1_PROBE_MAP";
  else if (!strcmp(disc, "SD2Y41") || !strcmp(disc, "SJ9E41") ||
           !strcmp(disc, "SJHE41") || !strcmp(disc, "SJDE41") || !strcmp(disc, "SJXE41")) {
    map_variable = "JD2_PROBE_MAP";
    join_with_a = !simulated;
  }
  if (socket >= 0) {
    // The direct loaders and the older on-disc menus tolerate different boot rates.
    boot_rate = map_variable ? 6 : 3;
    if (!strcmp(disc, "SJXE41")) boot_rate = 8;
  }
  if (!map_variable) map_variable = "JD2014_PROBE_MAP";
  if (map && strcmp(map, "-")) setenv(map_variable, map, 1);
  else unsetenv(map_variable);
  char *arguments[] = {program, sys, add_on ? nand : NULL, NULL};
  return jd_runtime_main(add_on ? 3 : 2, arguments);
}
