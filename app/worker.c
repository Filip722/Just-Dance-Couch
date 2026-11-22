#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "input.h"
#include "paths.h"
#include "remote_link.h"
#include "session.h"
#include "song_grid.h"

int jd_runtime_main(int count, char **arguments);

int session_send(int socket, enum SessionMessageType type, const void *data, unsigned size) {
  SessionMessage message = {.type = type, .size = size};
  if (size > sizeof message.data) return 0;
  if (size) memcpy(message.data, data, size);
  return send(socket, &message, 2 + size, MSG_DONTWAIT | MSG_NOSIGNAL) == (ssize_t)(2 + size);
}

int session_read(int socket, SessionMessage *message, int flags) {
  ssize_t bytes = recv(socket, message, sizeof *message, flags);
  if (bytes <= 0) return (int)bytes;
  if (bytes < 2 || message->size > sizeof message->data || bytes != 2 + message->size) {
    errno = EPROTO;
    return -1;
  }
  return 1;
}

static int bridge_socket = -1;
static int test_input;
enum { GAME_WINDOW_NORMAL, GAME_WINDOW_HIDDEN, GAME_WINDOW_READY, GAME_WINDOW_SHOWN };
static atomic_int game_window;
static atomic_int requested_song;
static int library_ready;
static const Edition *edition;

const char *jd_couch_requested_map(void) {
  int selected = atomic_load(&requested_song);
  return selected ? edition->songs[selected - 1].map : NULL;
}

void jd_couch_library_ready(void) {
  if (bridge_socket >= 0 && !library_ready) {
    library_ready = 1;
    session_send(bridge_socket, LIBRARY_READY, NULL, 0);
  }
}

int jd_couch_waiting_for_song(void) {
  return bridge_socket >= 0 && !atomic_load(&requested_song);
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

static int write_remote(void *unused, const uint8_t *report, uint32_t bytes) {
  (void)unused;
  return session_send(bridge_socket, REMOTE_OUTPUT, report, bytes) ? (int)bytes : -1;
}

static int keep_remote_open(void *unused, int guest_power_off) {
  (void)unused;
  (void)guest_power_off;
  return 0;
}

static void *read_remote(void *unused) {
  (void)unused;
  static const LinkOps operations = {write_remote, keep_remote_open};
  int remote = -1;
  SessionMessage message;
  while (session_read(bridge_socket, &message, 0) > 0) {
    switch (message.type) {
      case SELECT_SONG:
        if (message.size == 1 && edition && message.data[0] < edition->count)
          atomic_store(&requested_song, message.data[0] + 1);
        break;
      case REMOTE_CONNECTED:
        if (remote >= 0) link_lost(remote);
        if (message.size == 6) remote = link_attach(message.data, "Nintendo RVL-CNT-01", &operations, NULL);
        break;
      case REMOTE_DISCONNECTED:
        if (remote >= 0) link_lost(remote);
        remote = -1;
        break;
      case REMOTE_INPUT:
        if (remote >= 0 && message.size) link_input(remote, message.data, message.size);
        break;
      default:
        break;
    }
  }
  if (remote >= 0) link_lost(remote);
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
  char cache[PATH_MAX], state[PATH_MAX], sys[PATH_MAX], saves[PATH_MAX], builtin[24];
  for (int i = 0; i < edition_count; i++)
    if (!strcmp(disc, editions[i].disc)) edition = &editions[i];
  if (!edition && strcmp(disc, "SJDE41")) return 10;
  if (!root_path(cache, sizeof cache, "XDG_CACHE_HOME", ".cache") ||
      !root_path(state, sizeof state, "XDG_DATA_HOME", ".local/share") ||
      snprintf(sys, sizeof sys, "%s/discs/%s/sys", cache, disc) >= (int)sizeof sys ||
      snprintf(saves, sizeof saves, "%s/%s", state, disc) >= (int)sizeof saves) return 10;
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
  const char *map_variable = "JD2014_PROBE_MAP";
  if (!strcmp(disc, "SDNE41")) map_variable = "JD1_PROBE_MAP";
  else if (!strcmp(disc, "SD2Y41") || !strcmp(disc, "SJ9E41") || !strcmp(disc, "SJHE41"))
    map_variable = "JD2_PROBE_MAP";
  if (map && strcmp(map, "-")) setenv(map_variable, map, 1);
  else unsetenv(map_variable);
  char *arguments[] = {program, sys, NULL};
  return jd_runtime_main(2, arguments);
}
