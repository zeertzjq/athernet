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
#include "physical.h"

int main(int argc, char **argv) {
  bool binary = false;
  int max_frames = 10000 / FRAME_BITS;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--binary") == 0) {
      binary = true;
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

  phy_init();

  playback_start();
  for (int i = 0; i < max_frames; i++) {
    bool bits[FRAME_BITS];
    if (binary) {
      for (int i = 0; i < FRAME_BITS; i += 8) {
        int c = getchar();
        for (int k = 0; k < 8; k++) {
          bits[i + k] = c & (1 << k);
        }
      }
    } else {
      for (int i = 0; i < FRAME_BITS; i++) {
        char c;
        scanf(" %c", &c);
        bits[i] = c > '0';
      }
    }
    transmit_frame(bits);
  }
  playback_stop();

  return EXIT_SUCCESS;
}
