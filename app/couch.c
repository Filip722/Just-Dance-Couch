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

enum { WORKER_SOCKET = 3, STOP_GRACE_MS = 6000 };
enum ReturnReason { RUNNING, COMPLETED, CANCELLED };

typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SongGrid *grid;
  char *program;
  pid_t worker;
  int socket, remote, loopback, release_required, recap, playing, active, warm_next, library_ready;
  int edition, worker_edition, row, first_row, pending_song;
  int usb, usb_remote, usb_available;
  pthread_t usb_thread;
  uint16_t held;
  uint64_t launch_started, stop_at;
  uint8_t address[6];
  enum ReturnReason reason;
  const char *status;
} Browser;

static int has_remote(const Browser *browser) {
  return browser->remote >= 0 || browser->usb_remote;
}

static void write_remote(const Browser *browser, const uint8_t *report, unsigned size) {
  if (browser->remote >= 0) (void)write(browser->remote, report, size);
  else if (browser->usb_remote) session_send(browser->usb, REMOTE_OUTPUT, report, size);
}

static void menu_reports(const Browser *browser) {
  const uint8_t player_one_led[] = {0x11, 0x10};
  const uint8_t buttons_only[] = {0x12, 0x04, 0x30};
  write_remote(browser, player_one_led, sizeof player_one_led);
  write_remote(browser, buttons_only, sizeof buttons_only);
}

static void remote_connected(Browser *browser) {
  browser->release_required = 1;
  if (browser->active) session_send(browser->socket, REMOTE_CONNECTED, browser->address, sizeof browser->address);
  else menu_reports(browser);
}

static void remote_disconnected(Browser *browser) {
  browser->held = 0;
  browser->release_required = 1;
  if (browser->active) session_send(browser->socket, REMOTE_DISCONNECTED, NULL, 0);
}

static void find_remote(Browser *browser) {
  DIR *devices = opendir("/dev");
  if (!devices) return;
  struct dirent *entry;
  while ((entry = readdir(devices))) {
    if (strncmp(entry->d_name, "hidraw", 6)) continue;
    int fd = openat(dirfd(devices), entry->d_name, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) continue;
    struct hidraw_devinfo info;
    char name[256] = {0}, unique[64] = {0}, driver[64];
    if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
        link_hidraw_is_remote(info.bustype, (uint16_t)info.vendor, (uint16_t)info.product) &&
        ioctl(fd, HIDIOCGRAWNAME(sizeof name - 1), name) >= 0 &&
        !strncmp(name, "Nintendo RVL-CNT-01", sizeof "Nintendo RVL-CNT-01" - 1) &&
        ioctl(fd, HIDIOCGRAWUNIQ(sizeof unique - 1), unique) >= 0 &&
        link_hidraw_address(unique, browser->address) &&
        !(link_hidraw_driver("/sys/class/hidraw", entry->d_name, driver, sizeof driver) &&
          !strcmp(driver, "wiimote"))) {
      browser->remote = fd;
      remote_connected(browser);
      break;
    }
    close(fd);
  }
  closedir(devices);
}

static int start_worker(Browser *browser) {
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets)) {
    return 0;
  }

  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  if (!error) {
    error = posix_spawn_file_actions_adddup2(&actions, sockets[1], WORKER_SOCKET);
    char socket_number[12];
    snprintf(socket_number, sizeof socket_number, "%d", WORKER_SOCKET);
    char *arguments[] = {browser->program, "--worker", (char *)editions[browser->edition].disc, "-",
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
  if (!editions[browser->edition].songs[browser->row].map) return;
  if (!browser->loopback && !has_remote(browser)) return;
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
  if (!session_send(browser->socket, SELECT_SONG, &row, sizeof row)) {
    browser->status = "Could not start playback";
    return;
  }
  browser->active = 1;
  browser->launch_started = SDL_GetTicks();
  browser->status = NULL;
  browser->release_required = 1;
  if (!browser->loopback) session_send(browser->socket, REMOTE_CONNECTED, browser->address, sizeof browser->address);
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
  const int previous_edition = browser->edition;
  const int count = editions[browser->edition].count;
  const int previous = browser->row;
  const int column = previous % GRID_COLUMNS;
  if (buttons & BUTTON_LEFT) {
    if (column) browser->row--;
    else if (browser->edition > 0) {
      browser->edition--;
      browser->row = browser->first_row = 0;
    }
  } else if (buttons & BUTTON_RIGHT) {
    if (column + 1 < GRID_COLUMNS && previous + 1 < count) browser->row++;
    else if (browser->edition + 1 < EDITION_COUNT) {
      browser->edition++;
      browser->row = browser->first_row = 0;
    }
  } else if ((buttons & BUTTON_UP) && previous >= GRID_COLUMNS) {
    browser->row -= GRID_COLUMNS;
  } else if ((buttons & BUTTON_DOWN) && previous / GRID_COLUMNS < (count - 1) / GRID_COLUMNS) {
    browser->row = SDL_min(previous + GRID_COLUMNS, count - 1);
  }
  if (browser->edition != previous_edition) {
    browser->warm_next = 1;
    if (browser->worker) end_dance(browser, CANCELLED);
  }
  int row = browser->row / GRID_COLUMNS;
  if (row < browser->first_row) browser->first_row = row;
  if (row >= browser->first_row + GRID_ROWS) browser->first_row = row - GRID_ROWS + 1;
  if (buttons & (BUTTON_LEFT | BUTTON_RIGHT | BUTTON_UP | BUTTON_DOWN)) browser->status = NULL;
  if (buttons & BUTTON_A) start_dance(browser);
}

static void handle_report(Browser *browser, uint8_t *report, unsigned length) {
  if (length >= 3 && report[0] >= 0x20 && report[0] <= 0x3f) {
    const uint16_t actions = BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT | BUTTON_A | BUTTON_HOME;
    uint16_t buttons = (report[1] | (uint16_t)report[2] << 8) & actions;
    uint16_t edge = buttons & ~browser->held;
    browser->held = buttons;
    if (browser->release_required && !buttons) browser->release_required = 0;
    if (!browser->release_required && edge) pressed(browser, edge);
    report[2] &= (uint8_t)~(BUTTON_HOME >> 8);
  }
  if (browser->active && browser->reason == RUNNING) session_send(browser->socket, REMOTE_INPUT, report, length);
}

static void remote_reports(Browser *browser) {
  uint8_t report[32];
  for (;;) {
    ssize_t length = read(browser->remote, report, sizeof report);
    if (length < 0 && errno == EAGAIN) return;
    if (length < 0 && errno == EINTR) continue;
    if (length <= 0) {
      close(browser->remote);
      browser->remote = -1;
      remote_disconnected(browser);
      return;
    }
    handle_report(browser, report, length);
  }
}

static void usb_reports(Browser *browser) {
  SessionMessage message;
  int read;
  while ((read = session_read(browser->usb, &message, MSG_DONTWAIT)) > 0) {
    if (message.type == REMOTE_CONNECTED && message.size == sizeof browser->address) {
      memcpy(browser->address, message.data, message.size);
      browser->usb_remote = 1;
      remote_connected(browser);
    } else if (message.type == REMOTE_DISCONNECTED) {
      browser->usb_remote = 0;
      remote_disconnected(browser);
    } else if (message.type == REMOTE_INPUT && message.size && browser->usb_remote) {
      handle_report(browser, message.data, message.size);
    }
  }
  if (read == 0) {
    if (browser->usb_remote) remote_disconnected(browser);
    browser->usb_remote = browser->usb_available = 0;
    browser->status = "Nintendo USB adapter unavailable";
  }
}

static void update_worker(Browser *browser) {
  SessionMessage message;
  while (session_read(browser->socket, &message, MSG_DONTWAIT) > 0) {
    if (message.type == LIBRARY_READY) {
      browser->library_ready = 1;
      fprintf(stderr, "couch: native song library ready\n");
    }
    if (message.type == REMOTE_OUTPUT && browser->active && browser->reason == RUNNING)
      write_remote(browser, message.data, message.size);
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
  browser->warm_next = browser->reason != RUNNING;
  browser->release_required = 1;
  if (browser->reason == COMPLETED) browser->status = "Dance complete";
  else if (browser->reason == CANCELLED) browser->status = "Dance cancelled";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 4) browser->status = "Missing native game code";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 10) browser->status = "Playback could not start. Check the disc cache";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 2) browser->status = "The game could not load the selected routine";
  else browser->status = "Playback ended unexpectedly";
  menu_reports(browser);
  SDL_ShowWindow(browser->window);
  SDL_RaiseWindow(browser->window);
  fprintf(stderr, "couch: %s. Selected row: %s\n", browser->status, editions[browser->edition].songs[browser->row].title);
}

static void paint(const Browser *browser) {
  const char *hint = browser->status;
  if (!hint && !browser->loopback && !has_remote(browser))
    hint = browser->usb >= 0 ? "Wake your Wii Remote or press SYNC" : "Connect a Wii Remote";
  int preview = !browser->active && !browser->pending_song ? browser->row : -1;
  song_grid_preview(browser->grid, browser->edition, preview);
  int loading = browser->pending_song || (browser->active && browser->reason == RUNNING);
  if (loading && (!browser->library_ready || browser->pending_song)) hint = "Preparing the game";
  if (browser->active && !loading) hint = "Returning to the song grid";
  song_grid_paint(browser->grid, browser->edition, browser->row, browser->first_row,
                  loading, hint, browser->loopback);
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

  Browser browser = {.program = program, .remote = -1, .usb = -1, .loopback = loopback};
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
      if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        if (event.key.key == SDLK_ESCAPE) {
          if (browser.pending_song) browser.pending_song = 0;
          else if (browser.active) end_dance(&browser, browser.recap ? COMPLETED : CANCELLED);
          else quit = 1;
        }
        if (browser.loopback && !browser.active) switch (event.key.key) {
          case SDLK_LEFT: pressed(&browser, BUTTON_LEFT); break;
          case SDLK_RIGHT: pressed(&browser, BUTTON_RIGHT); break;
          case SDLK_UP: pressed(&browser, BUTTON_UP); break;
          case SDLK_DOWN: pressed(&browser, BUTTON_DOWN); break;
          case SDLK_RETURN: pressed(&browser, BUTTON_A); break;
        }
      }
    }
    uint64_t now = SDL_GetTicks();
    if (!usb_mode && !browser.loopback && browser.remote < 0 && now >= next_scan) {
      find_remote(&browser);
      next_scan = now + 1000;
    }
    if (browser.usb_available) usb_reports(&browser);
    if (browser.remote >= 0) remote_reports(&browser);
    if (browser.worker) update_worker(&browser);
    if (!quit && !browser.worker && browser.warm_next) {
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
  if (browser.remote >= 0) close(browser.remote);
  if (browser.usb >= 0) usb_remote_stop(browser.usb, browser.usb_thread);
  song_grid_destroy(browser.grid);
  SDL_DestroyRenderer(browser.renderer);
  SDL_DestroyWindow(browser.window);
  SDL_Quit();
  return 0;
}
