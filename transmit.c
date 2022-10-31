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
  int max_frames = 6250 * 8 / FRAME_BITS;

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

  phy_init();

  playback_start();
  for (int i = 0; i < max_frames; i++) {
    bool bits[FRAME_BITS];
    for (int i = 0; i < FRAME_BITS; i += 8) {
      int c = getchar();
      for (int k = 0; k < 8; k++) {
        bits[i + k] = c & (1 << k);
      }
    }
    transmit_frame(bits);
  }
  playback_stop();

  return EXIT_SUCCESS;
}
