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

extern char **environ;
int run_worker(char *program, const char *disc, const char *map, int socket, int simulated);

static const struct {
  const char *title, *map;
} routines[] = {{"Just Dance", "JustDance"}, {"Get Lucky", "GetLucky"}};
enum { ROW_COUNT = sizeof routines / sizeof routines[0], WORKER_SOCKET = 3, STOP_GRACE_MS = 6000 };
enum ReturnReason { RUNNING, COMPLETED, CANCELLED };

typedef struct {
  SDL_Window *window;
  SDL_Renderer *renderer;
  char *program;
  pid_t worker;
  int socket, remote, row, loopback, release_required, recap;
  uint16_t held;
  uint64_t launch_started, stop_at;
  uint8_t address[6];
  enum ReturnReason reason;
  const char *status;
} Browser;

static void menu_reports(int remote) {
  const uint8_t buttons_only[] = {0x12, 0x04, 0x30};
  if (remote >= 0) (void)write(remote, buttons_only, sizeof buttons_only);
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
      browser->release_required = 1;
      if (browser->worker) session_send(browser->socket, REMOTE_CONNECTED, browser->address, sizeof browser->address);
      else menu_reports(fd);
      break;
    }
    close(fd);
  }
  closedir(devices);
}

static void start_dance(Browser *browser) {
  if (!browser->loopback && browser->remote < 0) return;
  browser->launch_started = SDL_GetTicks();
  int sockets[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets)) {
    browser->status = "Could not start playback";
    return;
  }

  posix_spawn_file_actions_t actions;
  int error = posix_spawn_file_actions_init(&actions);
  if (!error) {
    error = posix_spawn_file_actions_adddup2(&actions, sockets[1], WORKER_SOCKET);
    char socket_number[12];
    snprintf(socket_number, sizeof socket_number, "%d", WORKER_SOCKET);
    char *arguments[] = {browser->program, "--worker", "SJOE41", (char *)routines[browser->row].map,
                         socket_number, browser->loopback ? "loopback" : "remote", NULL};
    if (!error) error = posix_spawn(&browser->worker, browser->program, &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
  }
  close(sockets[1]);
  if (error) {
    close(sockets[0]);
    browser->worker = 0;
    browser->status = "Could not start playback";
    return;
  }
  browser->socket = sockets[0];
  browser->reason = RUNNING;
  browser->recap = 0;
  browser->status = NULL;
  browser->release_required = 1;
  if (!browser->loopback) session_send(browser->socket, REMOTE_CONNECTED, browser->address, sizeof browser->address);
  SDL_HideWindow(browser->window);
  fprintf(stderr, "couch: launched %s\n", routines[browser->row].title);
}

static void end_dance(Browser *browser, enum ReturnReason reason) {
  if (browser->reason != RUNNING) return;
  browser->reason = reason;
  browser->stop_at = SDL_GetTicks() + STOP_GRACE_MS;
  kill(browser->worker, SIGTERM);
}

static void pressed(Browser *browser, uint16_t buttons) {
  if (browser->worker) {
    if (buttons & BUTTON_HOME) end_dance(browser, browser->recap ? COMPLETED : CANCELLED);
    else if (browser->recap && (buttons & BUTTON_A)) end_dance(browser, COMPLETED);
    return;
  }
  if ((buttons & BUTTON_UP) && browser->row > 0) browser->row--;
  if ((buttons & BUTTON_DOWN) && browser->row + 1 < ROW_COUNT) browser->row++;
  if (buttons & BUTTON_A) start_dance(browser);
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
      if (browser->worker) session_send(browser->socket, REMOTE_DISCONNECTED, NULL, 0);
      return;
    }
    if (length >= 3 && report[0] >= 0x20 && report[0] <= 0x3f) {
      const uint16_t actions = BUTTON_UP | BUTTON_DOWN | BUTTON_A | BUTTON_HOME;
      uint16_t buttons = (report[1] | (uint16_t)report[2] << 8) & actions;
      uint16_t edge = buttons & ~browser->held;
      browser->held = buttons;
      if (browser->release_required && !buttons) browser->release_required = 0;
      if (!browser->release_required && edge) pressed(browser, edge);
      report[2] &= (uint8_t)~(BUTTON_HOME >> 8);
    }
    if (browser->worker && browser->reason == RUNNING) session_send(browser->socket, REMOTE_INPUT, report, length);
  }
}

static void update_worker(Browser *browser) {
  SessionMessage message;
  while (session_read(browser->socket, &message, MSG_DONTWAIT) > 0) {
    if (message.type == REMOTE_OUTPUT && browser->remote >= 0 && browser->reason == RUNNING)
      (void)write(browser->remote, message.data, message.size);
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
  browser->release_required = 1;
  if (browser->reason == COMPLETED) browser->status = "Dance complete";
  else if (browser->reason == CANCELLED) browser->status = "Dance cancelled";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 4) browser->status = "Missing native game code";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 10) browser->status = "Playback could not start. Check the disc cache";
  else if (WIFEXITED(status) && WEXITSTATUS(status) == 2) browser->status = "The game could not load the selected routine";
  else browser->status = "Playback ended unexpectedly";
  menu_reports(browser->remote);
  SDL_ShowWindow(browser->window);
  SDL_RaiseWindow(browser->window);
  fprintf(stderr, "couch: %s. Selected row: %s\n", browser->status, routines[browser->row].title);
}

static void paint(const Browser *browser) {
  SDL_Renderer *renderer = browser->renderer;
  int width, height;
  SDL_GetRenderOutputSize(renderer, &width, &height);
  float scale = width >= 1000 ? 3 : 2;
  SDL_SetRenderScale(renderer, scale, scale);
  float usable = width / scale, left = usable * 0.12f, top = height / scale * 0.23f;
  SDL_SetRenderDrawColor(renderer, 16, 20, 30, 255);
  SDL_RenderClear(renderer);
  SDL_SetRenderDrawColor(renderer, 240, 243, 248, 255);
  SDL_RenderDebugText(renderer, left, top, "Just Dance 2014");
  for (int row = 0; row < ROW_COUNT; row++) {
    SDL_FRect box = {left - 5, top + 20 + 18 * row, usable - left * 2 + 10, 16};
    SDL_SetRenderDrawColor(renderer, row == browser->row ? 41 : 24, row == browser->row ? 87 : 34,
                          row == browser->row ? 138 : 49, 255);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, 246, 248, 250, 255);
    SDL_RenderDebugText(renderer, left, box.y + 4, routines[row].title);
  }
  SDL_SetRenderDrawColor(renderer, 179, 197, 218, 255);
  const char *hint = browser->status;
  if (!hint) hint = !browser->loopback && browser->remote < 0 ? "Connect a Wii Remote" : "D-pad to choose. A to start.";
  SDL_RenderDebugText(renderer, left, top + 66, hint);
  SDL_RenderPresent(renderer);
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
  if (count != 1 && !loopback) {
    fprintf(stderr, "usage: just-dance-couch [--loopback | --boot DISC_ID]\n");
    return 2;
  }

  Browser browser = {.program = program, .remote = -1, .loopback = loopback};
  if (!SDL_Init(SDL_INIT_VIDEO) ||
      !SDL_CreateWindowAndRenderer("Just Dance Couch", 960, 540, SDL_WINDOW_FULLSCREEN,
                                   &browser.window, &browser.renderer)) {
    fprintf(stderr, "couch: %s\n", SDL_GetError());
    return 1;
  }
  SDL_HideCursor();
  SDL_SetRenderVSync(browser.renderer, 1);
  uint64_t next_scan = 0;
  int quit = 0;
  while (!quit) {
    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, 16)) {
      if (event.type == SDL_EVENT_QUIT) quit = 1;
      if (browser.loopback && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !browser.worker) {
        switch (event.key.key) {
          case SDLK_UP: pressed(&browser, BUTTON_UP); break;
          case SDLK_DOWN: pressed(&browser, BUTTON_DOWN); break;
          case SDLK_RETURN: pressed(&browser, BUTTON_A); break;
          case SDLK_ESCAPE: quit = 1; break;
        }
      }
    }
    uint64_t now = SDL_GetTicks();
    if (!browser.loopback && browser.remote < 0 && now >= next_scan) {
      find_remote(&browser);
      next_scan = now + 1000;
    }
    if (browser.remote >= 0) remote_reports(&browser);
    if (browser.worker) update_worker(&browser);
    else paint(&browser);
  }
  if (browser.worker) {
    end_dance(&browser, CANCELLED);
    while (browser.worker) {
      update_worker(&browser);
      SDL_Delay(10);
    }
  }
  if (browser.remote >= 0) close(browser.remote);
  SDL_DestroyRenderer(browser.renderer);
  SDL_DestroyWindow(browser.window);
  SDL_Quit();
  return 0;
}
