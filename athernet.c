#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

int main(int argc, char **argv) {
  int transmit_bytes = 0;
  int receive_bytes = 0;

  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], S_LEN("--transmit=")) == 0) {
      transmit_bytes = atoi(argv[i] + LEN("--transmit="));
    } else if (strncmp(argv[i], S_LEN("--receive=")) == 0) {
      receive_bytes = atoi(argv[i] + LEN("--receive="));
    } else if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
      volume = atoi(argv[i] + LEN("--volume="));
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  if (transmit_bytes < 0) {
    transmit_bytes = 0;
  }
  if (receive_bytes < 0) {
    receive_bytes = 0;
  }
  if (transmit_bytes == 0 && receive_bytes == 0) {
    fprintf(stderr, "Nothing to do\n");
    return EXIT_FAILURE;
  }
  if (transmit_bytes > 0 && receive_bytes > 0) {
    fprintf(stderr, "CSMA not implemented yet\n");
    return EXIT_FAILURE;
  }

  int transmit_frames = (transmit_bytes * 8 + FRAME_BITS - 1) / FRAME_BITS;
  int receive_frames = (receive_bytes * 8 + FRAME_BITS - 1) / FRAME_BITS;

  phy_init();

  if (transmit_bytes > 0) {
    playback_start();
    while (--transmit_frames >= 0) {
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
  }

  if (receive_bytes > 0) {
    pthread_t capture_thread;
    pthread_create(&capture_thread, NULL, capture_loop, NULL);
    while (--receive_frames >= 0) {
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
  }

  return EXIT_SUCCESS;
}
