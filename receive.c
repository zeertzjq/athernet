#include <math.h>
#include <pthread.h>
#include <signal.h>
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
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }

  phy_init();

  pthread_t capture_thread;
  pthread_create(&capture_thread, NULL, capture_loop, NULL);

  for (int i = 0; i < max_frames; i++) {
    bool bits[FRAME_BITS];
    receive_frame(bits, NULL);
    for (int i = 0; i < FRAME_BITS; i += 8) {
      int c = 0;
      for (int k = 0; k < 8; k++) {
        c |= bits[i + k] << k;
      }
      putchar(c);
    }
    fflush(stdout);
  }
  capture_stopped = 1;

  pthread_join(capture_thread, NULL);

  return EXIT_SUCCESS;
}
