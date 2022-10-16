#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"

#define ZERO_LEN 480

static double carrier[RATE];
static size_t carrier_pos = 0;
static int16_t preamble[PREAMBLE_LEN];
static int16_t zero_buf[ZERO_LEN];
static int max_frames = 100;
static int volume = 16384;

static void transmit_bit(bool bit) {
  int16_t bit_buf[BIT_LEN];
  for (int i = 0; i < BIT_LEN; i++) {
    bit_buf[i] = carrier[carrier_pos + i] * (bit ? 1 : -1) * volume;
  }
  playback_write(bit_buf, BIT_LEN);
  carrier_pos += BIT_LEN;
  if (carrier_pos == RATE) {
    carrier_pos = 0;
  }
}

static void transmit_frame(bool *bits) {
  playback_write(preamble, PREAMBLE_LEN);
  for (int i = 0; i < FRAME_BITS; i++) {
    transmit_bit(bits[i]);
  }
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], S_LEN("--frames=")) == 0) {
      max_frames = atoi(argv[i] + LEN("--frames="));
    } else if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
      volume = atoi(argv[i] + LEN("--volume="));
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }

  GET_CARRIER(carrier);
  GET_PREAMBLE(preamble, volume);

  playback_start();
  for (int i = 0; i < max_frames; i++) {
    bool bits[FRAME_BITS];
    for (int i = 0; i < FRAME_BITS; i++) {
      char c;
      scanf(" %c", &c);
      bits[i] = c > '0';
    }
    playback_write(zero_buf, ZERO_LEN);
    transmit_frame(bits);
  }
  playback_stop();

  return EXIT_SUCCESS;
}
