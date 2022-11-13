#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

static int transmit_bytes = 0;
static int receive_bytes = 0;

static void input_frame(bool *const bits, const size_t len) {
  for (int pos = 0; pos < len; pos += 8) {
    if (--transmit_bytes >= 0) {
      decompose_u8(getchar(), bits + pos);
    }
  }
}

static void output_frame(const bool *const bits, const size_t len) {
  for (int pos = 0; pos < len; pos += 8) {
    if (--receive_bytes >= 0) {
      putchar(compose_u8(bits + pos));
    }
  }
  fflush(stdout);
}

int main(int argc, char **argv) {
  has_ack = false;
  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
      volume = atoi(argv[i] + LEN("--volume="));
    } else if (strncmp(argv[i], S_LEN("--transmit=")) == 0) {
      transmit_bytes = atoi(argv[i] + LEN("--transmit="));
    } else if (strncmp(argv[i], S_LEN("--receive=")) == 0) {
      receive_bytes = atoi(argv[i] + LEN("--receive="));
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
    fprintf(stderr, "Wrong executable\n");
    return EXIT_FAILURE;
  }
  const size_t payload_len = PHY_PAYLOAD_FIXED;
  const int transmit_cnt = (transmit_bytes * 8 + payload_len - 1) / payload_len;
  const int receive_cnt = (receive_bytes * 8 + payload_len - 1) / payload_len;

  phy_init();

  if (transmit_cnt > 0 && receive_cnt == 0) {
    playback_start();
    for (int i = 0; i < transmit_cnt; i++) {
      bool bits[PHY_PAYLOAD_FIXED];
      input_frame(bits, PHY_PAYLOAD_FIXED);
      phy_transmit_frame(bits, PHY_PAYLOAD_FIXED, false);
    }
    playback_stop();
  } else if (receive_cnt > 0 && transmit_cnt == 0) {
    pthread_t receive_thread;
    pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
    for (int i = 0; i < receive_cnt; i++) {
      bool bits[PHY_PAYLOAD_FIXED];
      phy_poll_frame(NULL);
      phy_receive_frame(bits);
      output_frame(bits, PHY_PAYLOAD_FIXED);
    }
    phy_receive_stopped = 1;
    pthread_join(receive_thread, NULL);
  }

  return EXIT_SUCCESS;
}
