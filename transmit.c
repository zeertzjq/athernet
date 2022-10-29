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
static int volume = 16384;

static void transmit_bit(bool bit) {
  int16_t bit_buf[BIT_LEN];
  for (int i = 0; i < BIT_LEN; i++) {
    bit_buf[i] = (bit ? 1 : -1) * volume * carrier[carrier_pos + i];
  }
  playback_write(bit_buf, BIT_LEN);
  carrier_pos += BIT_LEN;
  if (carrier_pos == RATE) {
    carrier_pos = 0;
  }
}

static void transmit_frame_plain(bool *bits) {
  playback_write(preamble, PREAMBLE_LEN);
  for (int i = 0; i < FRAME_BITS; i++) {
    transmit_bit(bits[i]);
  }
}

static void transmit_frame_hamming(bool *bits) {
  playback_write(preamble, PREAMBLE_LEN);
  for (int i = 0; i < FRAME_BITS / 2; i += 4) {
    bool out_bits[8] = {0,
                        bits[i] ^ bits[i + 1] ^ bits[i + 3],
                        bits[i] ^ bits[i + 2] ^ bits[i + 3],
                        bits[i],
                        bits[i + 1] ^ bits[i + 2] ^ bits[i + 3],
                        bits[i + 1],
                        bits[i + 2],
                        bits[i + 3]};
    for (int j = 1; j <= 7; j++) {
      out_bits[0] ^= out_bits[j];
    }
    for (int j = 0; j <= 7; j++) {
      transmit_bit(out_bits[j]);
    }
  }
}

int main(int argc, char **argv) {
  bool binary = false;
  void (*transmit_frame)(bool *) = transmit_frame_plain;
  int max_frames = 10000 / FRAME_BITS;
  size_t frame_bits = FRAME_BITS;
  int carrier_freq = 10000;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--binary") == 0) {
      binary = true;
    } else if (strcmp(argv[i], "--hamming") == 0) {
      transmit_frame = transmit_frame_hamming;
    } else if (strncmp(argv[i], S_LEN("--frames=")) == 0) {
      max_frames = atoi(argv[i] + LEN("--frames="));
    } else if (strncmp(argv[i], S_LEN("--carrier=")) == 0) {
      carrier_freq = atoi(argv[i] + LEN("--carrier="));
    } else if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
      volume = atoi(argv[i] + LEN("--volume="));
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  if (transmit_frame == transmit_frame_hamming) {
    max_frames *= 2;
    frame_bits /= 2;
  }

  GET_CARRIER(carrier, carrier_freq);
  GET_PREAMBLE(preamble, volume);

  playback_start();
  for (int i = 0; i < max_frames; i++) {
    bool bits[FRAME_BITS];
    if (binary) {
      for (int i = 0; i < frame_bits; i += 8) {
        int c = getchar();
        for (int k = 0; k < 8; k++) {
          bits[i + k] = c & (1 << k);
        }
      }
    } else {
      for (int i = 0; i < frame_bits; i++) {
        char c;
        scanf(" %c", &c);
        bits[i] = c > '0';
      }
    }
    playback_write(zero_buf, ZERO_LEN);
    transmit_frame(bits);
  }
  playback_stop();

  return EXIT_SUCCESS;
}
