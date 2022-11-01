#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

static void input_frame(bool *const bits) {
  for (int i = 0; i < FRAME_BITS; i += 8) {
    decompose_byte(getchar(), bits + i);
  }
}

static void output_frame(const bool *const bits) {
  for (int i = 0; i < FRAME_BITS; i += 8) {
    putchar(compose_byte(bits + i));
  }
  fflush(stdout);
}

int main(int argc, char **argv) {
  int transmit_bytes = 0;
  int receive_bytes = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--ack") == 0) {
      has_ack = true;
    } else if (strncmp(argv[i], S_LEN("--transmit=")) == 0) {
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

  int transmit_frames = (transmit_bytes * 8 + FRAME_BITS - 1) / FRAME_BITS;
  int receive_frames = (receive_bytes * 8 + FRAME_BITS - 1) / FRAME_BITS;

  phy_init();
  pthread_t receive_thread;

  if (transmit_bytes > 0 && receive_bytes == 0) {
    playback_start();
    if (has_ack) {
      pthread_create(&receive_thread, NULL, receive_loop, NULL);
    }
    while (--transmit_frames >= 0) {
      bool bits[FRAME_BITS];
      input_frame(bits);
      do {
        transmit_frame(bits);
      } while (has_ack && !receive_ack(50000));
    }
    if (has_ack) {
      receive_stopped = 1;
      pthread_join(receive_thread, NULL);
    }
    playback_stop();
  } else if (receive_bytes > 0 && transmit_bytes == 0) {
    pthread_create(&receive_thread, NULL, receive_loop, NULL);
    if (has_ack) {
      playback_start();
    }
    while (--receive_frames >= 0) {
      bool bits[FRAME_BITS];
      receive_frame(bits);
      if (has_ack) {
        transmit_ack();
      }
      output_frame(bits);
    }
    if (has_ack) {
      playback_stop();
    }
    receive_stopped = 1;
    pthread_join(receive_thread, NULL);
  } else {
    fprintf(stderr, "CSMA not implemented yet\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
