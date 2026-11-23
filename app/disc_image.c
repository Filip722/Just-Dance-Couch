#include "disc_image.h"

#include <stdio.h>
#include <string.h>

#include "nod.h"

enum { REGION_OFFSET = 0x4e000 };

static uint8_t expected_header[8], expected_region[4];
static NodHandle *partition;
static NodPartitionMeta metadata;

void jd_image_expect(const uint8_t header[8], const uint8_t region[4]) {
  memcpy(expected_header, header, sizeof expected_header);
  memcpy(expected_region, region, sizeof expected_region);
}

static int read_at(NodHandle *image, uint64_t offset, uint8_t *output, size_t bytes) {
  if (offset > INT64_MAX || nod_seek(image, (int64_t)offset, SEEK_SET) != (int64_t)offset) return 0;
  while (bytes) {
    int64_t count = nod_read(image, output, bytes);
    if (count <= 0) return 0;
    output += count;
    bytes -= (size_t)count;
  }
  return 1;
}

void jd_image_close(void) {
  if (partition) nod_free(partition);
  partition = NULL;
}

int jd_image_open(const char *path, uint8_t *raw, size_t raw_bytes) {
  if (!expected_header[0]) return -1;

  NodHandle *disc = NULL;
  if (nod_disc_open(path, NULL, &disc) != NOD_RESULT_OK) {
    fprintf(stderr, "drive: cannot open %s: %s\n", path, nod_error_message());
    return 0;
  }
  int matches = raw_bytes >= REGION_OFFSET + sizeof expected_region &&
                read_at(disc, 0, raw, raw_bytes) &&
                !memcmp(raw, expected_header, sizeof expected_header) &&
                !memcmp(raw + REGION_OFFSET, expected_region, sizeof expected_region);
  int opened = matches &&
               nod_disc_open_partition_kind(disc, NOD_PARTITION_KIND_DATA, NULL, &partition) == NOD_RESULT_OK &&
               nod_partition_meta(partition, &metadata) == NOD_RESULT_OK;
  nod_free(disc);
  if (!opened) {
    if (!matches)
      fprintf(stderr, "drive: %s must contain %.6s revision %u in the supported region\n",
              path, expected_header, (unsigned)expected_header[7]);
    else
      fprintf(stderr, "drive: cannot read the Wii data partition in %s: %s\n", path, nod_error_message());
    jd_image_close();
    return 0;
  }
  fprintf(stderr, "drive: reading disc data directly from %s\n", path);
  return 1;
}

int jd_image_read(uint64_t offset, uint32_t bytes, uint8_t *output) {
  return read_at(partition, offset, output, bytes);
}

size_t jd_image_metadata(int tmd, uint8_t *output, size_t limit) {
  NodBlob blob = tmd ? metadata.raw_tmd : metadata.raw_ticket;
  size_t bytes = blob.size < limit ? blob.size : limit;
  memcpy(output, blob.data, bytes);
  return bytes;
}
