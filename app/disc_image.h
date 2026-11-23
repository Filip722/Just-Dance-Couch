#pragma once

#include <stddef.h>
#include <stdint.h>

void jd_image_expect(const uint8_t header[8], const uint8_t region[4]);
int jd_image_open(const char *path, uint8_t *raw, size_t raw_bytes);
void jd_image_close(void);
int jd_image_read(uint64_t offset, uint32_t bytes, uint8_t *output);
size_t jd_image_metadata(int tmd, uint8_t *output, size_t limit);
