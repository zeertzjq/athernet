#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "common.h"

void capture_start(void) {}

void capture_stop(void) {}

void capture_read(int16_t *buf, size_t len) {
  usleep(len * 1000000 / RATE);
  for (int i = 0; i < len; i++) {
    scanf("%hd", &buf[i]);
  }
}

void playback_start(void) {}

void playback_stop(void) {}

void playback_write(int16_t *buf, size_t len) {
  for (int i = 0; i < len; i++) {
    printf("%hd ", buf[i]);
  }
  usleep(len * 1000000 / RATE);
}
