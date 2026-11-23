#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/hidraw.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "input.h"
#include "remote_link_hidraw.h"
#include "session.h"
#include "song_grid.h"
#include "usb_remote.h"

extern char **environ;
int run_worker(char *program, const char *disc, const char *map, int socket, int simulated);

enum {
  WORKER_SOCKET = 3, STOP_GRACE_MS = 6000,
  DPAD_BUTTONS = BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN,
  DPAD_REPEAT_DELAY_MS = 350, DPAD_REPEAT_INTERVAL_MS = 110,
  REMOTE_LEDS = 0x11, PLAYER_ONE_LED = 0x10,
};
enum ReturnReason { RUNNING, COMPLETED, CANCELLED };

typedef struct {
  int fd, connected;
  uint8_t address[REMOTE_ADDRESS_BYTES];
} BrowserRemote;

typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SongGrid *grid;
  char *program;
  pid_t worker;
  int socket, loopback, release_required, recap, playing, active, warm_next, library_ready;
  int edition, worker_edition, row, first_row, pending_song, edition_focused;
  int usb, usb_available;
  pthread_t usb_thread;
  uint16_t held;
  uint64_t launch_started, stop_at, repeat_at;
  BrowserRemote remotes[REMOTE_PLAYERS];
  enum ReturnReason reason;
  const char *status;
} Browser;

static int first_remote(const Browser *browser) {
  for (int player = 0; player < REMOTE_PLAYERS; player++)
    if (browser->remotes[player].connected) return player;
  return -1;
}

static int free_remote(const Browser *browser) {
  for (int player = 0; player < REMOTE_PLAYERS; player++)
    if (!browser->remotes[player].connected) return player;
  return -1;
}

static void write_remote(const Browser *browser, unsigned player, const uint8_t *report, unsigned size) {
  const BrowserRemote *remote = &browser->remotes[player];
  if (!remote->connected) return;
  if (remote->fd >= 0) (void)write(remote->fd, report, size);
  else if (browser->usb_available) session_send_player(browser->usb, REMOTE_OUTPUT, player, report, size);
}

static void menu_reports(const Browser *browser, unsigned player) {
  const uint8_t player_led[] = {REMOTE_LEDS, PLAYER_ONE_LED << player};
  const uint8_t buttons_only[] = {0x12, 0x04, 0x30};
  write_remote(browser, player, player_led, sizeof player_led);
  write_remote(browser, player, buttons_only, sizeof buttons_only);
}

static void set_usb_discovery(const Browser *browser, uint8_t enabled) {
  if (browser->usb_available) session_send(browser->usb, USB_DISCOVERY, &enabled, sizeof enabled);
}

static void attach_to_worker(const Browser *browser, unsigned player) {
  const BrowserRemote *remote = &browser->remotes[player];
  session_send_player(browser->socket, REMOTE_CONNECTED, player, remote->address, sizeof remote->address);
}

static void remote_connected(Browser *browser, int player, const uint8_t *address) {
  BrowserRemote *remote = &browser->remotes[player];
  memcpy(remote->address, address, sizeof remote->address);
  remote->connected = 1;
  if (player == first_remote(browser)) {
    browser->held = 0;
    browser->release_required = 1;
  }
  menu_reports(browser, player);
  if (browser->active && browser->reason == RUNNING) attach_to_worker(browser, player);
}

static void remote_disconnected(Browser *browser, int player) {
  BrowserRemote *remote = &browser->remotes[player];
  if (!remote->connected) return;
  if (player == first_remote(browser)) {
    browser->held = 0;
    browser->release_required = 1;
  }
  remote->connected = 0;
  if (browser->active && browser->reason == RUNNING)
    session_send_player(browser->socket, REMOTE_DISCONNECTED, player, NULL, 0);
}

static int has_address(const Browser *browser, const uint8_t *address) {
  for (int player = 0; player < REMOTE_PLAYERS; player++) {
    const BrowserRemote *remote = &browser->remotes[player];
    if (remote->connected && !memcmp(address, remote->address, sizeof remote->address)) return 1;
  }
  return 0;
}

static void find_remotes(Browser *browser) {
  int player = free_remote(browser);
  if (player < 0) return;
  DIR *devices = opendir("/dev");
  if (!devices) return;
  struct dirent *entry;
  while (player >= 0 && (entry = readdir(devices))) {
    if (strncmp(entry->d_name, "hidraw", 6)) continue;
    int fd = openat(dirfd(devices), entry->d_name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    struct hidraw_devinfo info;
    char name[256] = {0}, unique[64] = {0}, driver[64];
    uint8_t address[REMOTE_ADDRESS_BYTES];
    if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
        link_hidraw_is_remote(info.bustype, (uint16_t)info.vendor, (uint16_t)info.product) &&
        ioctl(fd, HIDIOCGRAWNAME(sizeof name - 1), name) >= 0 &&
        !strncmp(name, "Nintendo RVL-CNT-01", sizeof "Nintendo RVL-CNT-01" - 1) &&
        ioctl(fd, HIDIOCGRAWUNIQ(sizeof unique - 1), unique) >= 0 &&
        link_hidraw_address(unique, address) && !has_address(browser, address) &&
        !(link_hidraw_driver("/sys/class/hidraw", entry->d_name, driver, sizeof driver) &&
          !strcmp(driver, "wiimote"))) {
      browser->remotes[player].fd = fd;
      remote_connected(browser, player, address);
      player = free_remote(browser);
    } else close(fd);
  }
  closedir(devices);
}

static int start_worker(Browser *browser) {
  const char *disc = editions[browser->edition].disc;
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets)) {
    return 0;
  }

  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  if (!error) {
    error = posix_spawn_file_actions_adddup2(&actions, sockets[1], WORKER_SOCKET);
    char socket_number[12], dlc_row[12];
    snprintf(socket_number, sizeof socket_number, "%d", WORKER_SOCKET);
    snprintf(dlc_row, sizeof dlc_row, "%d", browser->row);
    char *arguments[] = {browser->program, "--worker", disc ? (char *)disc : "DLC", disc ? "-" : dlc_row,
                         socket_number, browser->loopback ? "loopback" : "remote", NULL};
    if (!error) error = posix_spawn(&browser->worker, browser->program, &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
  }
  close(sockets[1]);
  if (error) {
    close(sockets[0]);
    browser->worker = 0;
    return 0;
  }
  browser->socket = sockets[0];
  browser->worker_edition = browser->edition;
  browser->library_ready = 0;
  browser->reason = RUNNING;
  browser->recap = 0;
  return 1;
}

static void end_dance(Browser *browser, enum ReturnReason reason);

static void start_dance(Browser *browser) {
  const char *map = editions[browser->edition].songs[browser->row].map;
  if (!map) return;
  if (!browser->loopback && !browser->remotes[0].connected) return;
  if (browser->worker && (browser->worker_edition != browser->edition || browser->reason != RUNNING)) {
    browser->pending_song = 1;
    end_dance(browser, CANCELLED);
    return;
  }
  if (!browser->worker && !start_worker(browser)) {
    browser->status = "Could not start playback";
    return;
  }
  song_grid_preview(browser->grid, browser->edition, -1);
  uint8_t row = browser->row;
  if (editions[browser->edition].disc && !session_send(browser->socket, SELECT_SONG, &row, sizeof row)) {
    browser->status = "Could not start playback";
    return;
  }
  browser->active = 1;
  browser->launch_started = SDL_GetTicks();
  browser->status = NULL;
  browser->release_required = 1;
  set_usb_discovery(browser, 0);
  for (unsigned player = 0; player < REMOTE_PLAYERS; player++)
    if (browser->remotes[player].connected) attach_to_worker(browser, player);
  fprintf(stderr, "couch: launched %s\n", editions[browser->edition].songs[browser->row].title);
}

static void end_dance(Browser *browser, enum ReturnReason reason) {
  if (browser->reason != RUNNING) return;
  browser->reason = reason;
  browser->stop_at = SDL_GetTicks() + STOP_GRACE_MS;
  kill(browser->worker, SIGTERM);
  if (browser->playing) {
    browser->playing = 0;
    SDL_ShowWindow(browser->window);
    SDL_RaiseWindow(browser->window);
  }
}

static void change_edition(Browser *browser, int step) {
  int edition = browser->edition + step;
  if (edition < 0 || edition >= edition_count) return;
  browser->edition = edition;
  browser->row = browser->first_row = 0;
  browser->warm_next = editions[edition].disc != NULL;
  if (browser->worker) end_dance(browser, CANCELLED);
}

static void pressed(Browser *browser, uint16_t buttons) {
  if (browser->pending_song) {
    if (buttons & BUTTON_HOME) browser->pending_song = 0;
    return;
  }
  if (browser->active) {
    if (buttons & BUTTON_HOME) end_dance(browser, browser->recap ? COMPLETED : CANCELLED);
    else if (browser->recap && (buttons & BUTTON_A)) end_dance(browser, COMPLETED);
    return;
  }
  if (buttons & DPAD_BUTTONS) browser->status = NULL;
  if (browser->edition_focused) {
    if (buttons & BUTTON_LEFT) change_edition(browser, -1);
    else if (buttons & BUTTON_RIGHT) change_edition(browser, 1);
    else if (buttons & (BUTTON_DOWN | BUTTON_A)) browser->edition_focused = 0;
    return;
  }
  const int count = editions[browser->edition].count;
  const int previous = browser->row;
  const int column = previous % GRID_COLUMNS;
  if (buttons & BUTTON_LEFT) {
    if (column) browser->row--;
    else change_edition(browser, -1);
  } else if (buttons & BUTTON_RIGHT) {
    if (column + 1 < GRID_COLUMNS && previous + 1 < count) browser->row++;
    else change_edition(browser, 1);
  } else if (buttons & BUTTON_UP) {
    if (previous < GRID_COLUMNS) {
      browser->edition_focused = 1;
      return;
    }
    browser->row -= GRID_COLUMNS;
  } else if ((buttons & BUTTON_DOWN) && previous / GRID_COLUMNS < (count - 1) / GRID_COLUMNS) {
    browser->row = SDL_min(previous + GRID_COLUMNS, count - 1);
  }
  int row = browser->row / GRID_COLUMNS;
  if (row < browser->first_row) browser->first_row = row;
  if (row >= browser->first_row + GRID_ROWS) browser->first_row = row - GRID_ROWS + 1;
  if (buttons & BUTTON_A) start_dance(browser);
}

static void update_buttons(Browser *browser, uint16_t buttons) {
  uint16_t changed = buttons ^ browser->held;
  browser->held = buttons;
  if (changed & DPAD_BUTTONS) browser->repeat_at = SDL_GetTicks() + DPAD_REPEAT_DELAY_MS;
  if (!browser->loopback && browser->release_required) {
    if (!buttons) browser->release_required = 0;
    return;
  }
  if (buttons & changed) pressed(browser, buttons & changed);
}

static void repeat_dpad(Browser *browser, uint64_t now) {
  uint16_t directions = browser->held & DPAD_BUTTONS;
  if (directions && !browser->active && !browser->pending_song &&
      (browser->loopback || !browser->release_required) && now >= browser->repeat_at) {
    browser->repeat_at = now + DPAD_REPEAT_INTERVAL_MS;
    pressed(browser, directions);
  }
}

static uint16_t keyboard_button(SDL_Keycode key) {
  switch (key) {
    case SDLK_LEFT: return BUTTON_LEFT;
    case SDLK_RIGHT: return BUTTON_RIGHT;
    case SDLK_UP: return BUTTON_UP;
    case SDLK_DOWN: return BUTTON_DOWN;
    case SDLK_RETURN: return BUTTON_A;
    default: return 0;
  }
}

static void handle_report(Browser *browser, int player, uint8_t *report, unsigned length) {
  if (length >= 3 && report[0] >= 0x20 && report[0] <= 0x3f) {
    uint16_t controls = report[1] | (uint16_t)report[2] << 8;
    const uint16_t actions = DPAD_BUTTONS | BUTTON_A | BUTTON_HOME;
    if (player == first_remote(browser)) update_buttons(browser, controls & actions);
    else if (browser->active && !browser->playing) {
      // Keep motion bits while the primary remote drives the hidden menus.
      const uint16_t buttons = DPAD_BUTTONS | BUTTON_PLUS | BUTTON_TWO | BUTTON_ONE |
                               BUTTON_B | BUTTON_A | BUTTON_MINUS | BUTTON_HOME;
      controls &= ~buttons;
    }
    report[1] = controls;
    report[2] = (controls & ~BUTTON_HOME) >> 8;
  }
  if (browser->active && browser->reason == RUNNING)
    session_send_player(browser->socket, REMOTE_INPUT, player, report, length);
}

static void remote_reports(Browser *browser, int player) {
  BrowserRemote *remote = &browser->remotes[player];
  uint8_t report[32];
  for (;;) {
    ssize_t length = read(remote->fd, report, sizeof report);
    if (length < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
    if (length < 0 && errno == EINTR) continue;
    if (length <= 0) {
      close(remote->fd);
      remote->fd = -1;
      remote_disconnected(browser, player);
      return;
    }
    handle_report(browser, player, report, length);
  }
}

static void usb_reports(Browser *browser) {
  SessionMessage message;
  int read;
  while ((read = session_read(browser->usb, &message, MSG_DONTWAIT)) > 0) {
    int player = message.player;
    if (message.type == REMOTE_CONNECTED && message.size == REMOTE_ADDRESS_BYTES)
      remote_connected(browser, player, message.data);
    else if (message.type == REMOTE_DISCONNECTED)
      remote_disconnected(browser, player);
    else if (message.type == REMOTE_INPUT && message.size && browser->remotes[player].connected)
      handle_report(browser, player, message.data, message.size);
  }
  if (read == 0) {
    for (int player = 0; player < REMOTE_PLAYERS; player++) remote_disconnected(browser, player);
    browser->usb_available = 0;
    browser->status = "Nintendo USB adapter unavailable";
  }
}

static void update_worker(Browser *browser) {
  SessionMessage message;
  while (session_read(browser->socket, &message, MSG_DONTWAIT) > 0) {
    if (message.type == LIBRARY_READY && browser->reason == RUNNING) {
      browser->library_ready = 1;
      fprintf(stderr, "couch: native song library ready\n");
    }
    if (message.type == REMOTE_OUTPUT && message.size && browser->active && browser->reason == RUNNING) {
      if (message.data[0] == REMOTE_LEDS && message.size >= 2)
        message.data[1] = (message.data[1] & 0x0f) | (PLAYER_ONE_LED << message.player);
      write_remote(browser, message.player, message.data, message.size);
    }
    if (message.type == GAME_VISIBLE && browser->reason == RUNNING) {
      browser->playing = 1;
      SDL_HideWindow(browser->window);
      fprintf(stderr, "couch: showing the selected dance after %llu ms\n",
              (unsigned long long)(SDL_GetTicks() - browser->launch_started));
    }
    if (message.type == DANCE_STARTED) {
      fprintf(stderr, "couch: first original move score after %llu ms\n",
              (unsigned long long)(SDL_GetTicks() - browser->launch_started));
    }
    if (message.type == RECAP_ENTERED) {
      browser->recap = 1;
      fprintf(stderr, "couch: original recap entered\n");
    }
    if (message.type == TEST_BUTTONS && message.size == sizeof(uint16_t) && browser->loopback) {
      uint16_t buttons;
      memcpy(&buttons, message.data, sizeof buttons);
      pressed(browser, buttons);
    }
  }
  if (browser->reason != RUNNING && SDL_GetTicks() >= browser->stop_at) kill(browser->worker, SIGKILL);
  int status;
  if (waitpid(browser->worker, &status, WNOHANG) <= 0) return;
  close(browser->socket);
  browser->worker = 0;
  browser->playing = 0;
  if (!browser->active) {
    if (browser->pending_song) {
      browser->pending_song = 0;
      start_dance(browser);
    }
    return;
  }
  browser->active = 0;
  browser->warm_next = browser->reason != RUNNING && editions[browser->edition].disc != NULL;
  browser->release_required = 1;
  if (browser->reason == COMPLETED) browser->status = "Dance complete";
  else if (browser->reason == CANCELLED) browser->status = "Dance cancelled";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 4) browser->status = "Missing native game code";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 10) browser->status = "Playback could not start. Check the disc cache";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 2) browser->status = "The game could not load the selected routine";
  else browser->status = "Playback ended unexpectedly";
  set_usb_discovery(browser, 1);
  for (unsigned player = 0; player < REMOTE_PLAYERS; player++)
    if (browser->remotes[player].connected) menu_reports(browser, player);
  SDL_ShowWindow(browser->window);
  SDL_RaiseWindow(browser->window);
  fprintf(stderr, "couch: %s. Selected row: %s\n", browser->status, editions[browser->edition].songs[browser->row].title);
}

static void paint(const Browser *browser) {
  const char *hint = browser->status;
  if (!browser->loopback && !browser->remotes[0].connected) {
    if (first_remote(browser) > 0) hint = "Connect a Wii Remote as Player 1 to start";
    else if (!hint) hint = browser->usb >= 0 ? "Wake your Wii Remote or press SYNC" : "Connect a Wii Remote";
  }
  int preview = !browser->active && !browser->pending_song && !browser->edition_focused ? browser->row : -1;
  song_grid_preview(browser->grid, browser->edition, preview);
  int loading = browser->pending_song || (browser->active && browser->reason == RUNNING);
  if (loading && (!browser->library_ready || browser->pending_song)) hint = "Preparing the game";
  if (browser->active && !loading) hint = "Returning to the song grid";
  song_grid_paint(browser->grid, browser->edition, browser->row, browser->first_row,
                  browser->edition_focused, loading, hint, browser->loopback);
}

int main(int count, char **arguments) {
  char program[PATH_MAX];
  ssize_t program_size = readlink("/proc/self/exe", program, sizeof program - 1);
  if (program_size < 0 || program_size >= (ssize_t)sizeof program - 1) return 10;
  program[program_size] = 0;
  if (count == 6 && !strcmp(arguments[1], "--worker"))
    return run_worker(program, arguments[2], arguments[3], atoi(arguments[4]), !strcmp(arguments[5], "loopback"));
  if (count == 3 && !strcmp(arguments[1], "--boot"))
    return run_worker(program, arguments[2], NULL, -1, 1);
  int loopback = count == 2 && !strcmp(arguments[1], "--loopback");
  int usb_mode = count == 2 && !strcmp(arguments[1], "--usb-wii");
  if (count != 1 && !loopback && !usb_mode) {
    fprintf(stderr, "usage: just-dance-couch [--usb-wii | --loopback | --boot DISC_ID]\n");
    return 2;
  }

  Browser browser = {.program = program, .usb = -1, .loopback = loopback};
  for (unsigned player = 0; player < REMOTE_PLAYERS; player++) browser.remotes[player].fd = -1;
  if (!SDL_Init(SDL_INIT_VIDEO) ||
      !SDL_CreateWindowAndRenderer("Just Dance Couch", 960, 540, SDL_WINDOW_FULLSCREEN,
                                   &browser.window, &browser.renderer)) {
    fprintf(stderr, "couch: %s\n", SDL_GetError());
    return 1;
  }
  browser.grid = song_grid_create(browser.renderer);
  if (!browser.grid) {
    SDL_Quit();
    return 1;
  }
  SDL_HideCursor();
  SDL_SetRenderVSync(browser.renderer, 1);
  if (usb_mode) {
    browser.usb = usb_remote_start(&browser.usb_thread);
    browser.usb_available = browser.usb >= 0;
    if (!browser.usb_available) browser.status = "Could not start Nintendo USB adapter";
  }
  uint64_t next_scan = 0;
  int quit = 0;
  browser.warm_next = 1;
  while (!quit) {
    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, 16)) {
      if (event.type == SDL_EVENT_QUIT) quit = 1;
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_ESCAPE) {
        if (browser.pending_song) browser.pending_song = 0;
        else if (browser.active) end_dance(&browser, browser.recap ? COMPLETED : CANCELLED);
        else quit = 1;
      }
      if (browser.loopback) {
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) browser.held = 0;
        if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && !event.key.repeat) {
          uint16_t button = keyboard_button(event.key.key);
          if (button && (!event.key.down || !browser.active))
            update_buttons(&browser, event.key.down ? browser.held | button : browser.held & ~button);
        }
      }
    }
    uint64_t now = SDL_GetTicks();
    if (!usb_mode && !browser.loopback && now >= next_scan) {
      find_remotes(&browser);
      next_scan = now + 1000;
    }
    if (browser.usb_available) usb_reports(&browser);
    for (int player = 0; player < REMOTE_PLAYERS; player++)
      if (browser.remotes[player].fd >= 0) remote_reports(&browser, player);
    if (!quit) repeat_dpad(&browser, now);
    if (browser.worker) update_worker(&browser);
    // Start warming once horizontal browsing stops on an edition.
    if (!quit && !browser.worker && browser.warm_next && !(browser.held & (BUTTON_LEFT | BUTTON_RIGHT))) {
      browser.warm_next = 0;
      start_worker(&browser);
    }
    if (!browser.playing) paint(&browser);
  }
  browser.pending_song = 0;
  if (browser.worker) {
    end_dance(&browser, CANCELLED);
    while (browser.worker) {
      update_worker(&browser);
      SDL_Delay(10);
    }
  }
  for (unsigned player = 0; player < REMOTE_PLAYERS; player++)
    if (browser.remotes[player].fd >= 0) close(browser.remotes[player].fd);
  if (browser.usb >= 0) usb_remote_stop(browser.usb, browser.usb_thread);
  song_grid_destroy(browser.grid);
  SDL_DestroyRenderer(browser.renderer);
  SDL_DestroyWindow(browser.window);
  SDL_Quit();
  return 0;
}
