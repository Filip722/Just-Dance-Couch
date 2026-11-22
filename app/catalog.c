#include <string.h>

#include "song_grid.h"

#include "collection_catalog.h"

const int edition_count = sizeof editions / sizeof editions[0];

const Edition *edition_for_disc(const char *disc) {
  for (int i = 0; i < edition_count; i++)
    if (editions[i].disc && !strcmp(editions[i].disc, disc)) return &editions[i];
  return NULL;
}
