#pragma once

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static inline int directory_tree(char *path) {
  for (char *slash = path + 1; (slash = strchr(slash, '/')); slash++) {
    *slash = 0;
    int ok = mkdir(path, 0700) == 0 || errno == EEXIST;
    *slash = '/';
    if (!ok) return 0;
  }
  return mkdir(path, 0700) == 0 || errno == EEXIST;
}

static inline int root_path(char *result, size_t capacity, const char *variable, const char *fallback) {
  const char *xdg = getenv(variable);
  if (xdg && xdg[0] == '/') return snprintf(result, capacity, "%s/just-dance-couch", xdg) < (int)capacity;
  const char *home = getenv("HOME");
  return home && snprintf(result, capacity, "%s/%s/just-dance-couch", home, fallback) < (int)capacity;
}
