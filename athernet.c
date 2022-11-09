#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

static int transmit_bytes = 0;
static int receive_bytes = 0;

static void input_frame(bool *const bits, const size_t len) {
  for (int i = 0; i < len; i += 8) {
    if (--transmit_bytes >= 0) {
      decompose_byte(getchar(), bits + i);
    }
  }
}

static void output_frame(const bool *const bits, const size_t len) {
  for (int i = 0; i < len; i += 8) {
    if (--receive_bytes >= 0) {
      putchar(compose_byte(bits + i));
    }
  }
  fflush(stdout);
}

int main(int argc, char **argv) {
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
  if (transmit_bytes > 0 && receive_bytes > 0) {
    has_ack = true;
  }
  const size_t payload_len = has_ack ? 480 : PHY_PAYLOAD_FIXED;
  const int transmit_cnt = (transmit_bytes * 8 + payload_len - 1) / payload_len;
  const int receive_cnt = (receive_bytes * 8 + payload_len - 1) / payload_len;

  phy_init();
  pthread_t receive_thread;

  if (!has_ack) {
    if (transmit_cnt > 0 && receive_cnt == 0) {
      playback_start();
      for (int i = 0; i < transmit_cnt; i++) {
        bool bits[PHY_PAYLOAD_FIXED];
        input_frame(bits, payload_len);
        transmit_frame(bits, payload_len);
      }
      playback_stop();
    } else if (receive_cnt > 0 && transmit_cnt == 0) {
      pthread_create(&receive_thread, NULL, receive_loop, NULL);
      for (int i = 0; i < receive_cnt; i++) {
        bool bits[PHY_PAYLOAD_MAX];
        receive_frame(bits, payload_len, NULL);
        output_frame(bits, payload_len);
      }
      receive_stopped = 1;
      pthread_join(receive_thread, NULL);
    }
    return EXIT_SUCCESS;
  }

  const size_t frame_len = payload_len + 8;

  if (transmit_cnt > 0 && receive_cnt == 0) {
    playback_start();
    pthread_create(&receive_thread, NULL, receive_loop, NULL);
    for (int i = 0; i < transmit_cnt; i++) {
      const uint8_t ack_num = i & 0xFF;
      bool bits[PHY_PAYLOAD_MAX];
      input_frame(bits, payload_len);
      decompose_byte(ack_num, bits + payload_len);
      int num_retries = 5;
      do {
        transmit_frame(bits, frame_len);
        suseconds_t timeout = 50000;
        bool ack_bits[8];
        receive_frame(ack_bits, 8, &timeout);
        if (timeout >= 0 && compose_byte(ack_bits) == ack_num) {
          break;
        }
      } while (--num_retries >= 0);
      if (num_retries < 0) {
        fprintf(stderr, "link error\n");
        break;
      }
    }
    receive_stopped = 1;
    pthread_join(receive_thread, NULL);
    playback_stop();
  } else if (receive_cnt > 0 && transmit_cnt == 0) {
    pthread_create(&receive_thread, NULL, receive_loop, NULL);
    playback_start();
    for (int i = 0; i < receive_cnt; i++) {
      const uint8_t ack_num = i & 0xFF;
      bool bits[PHY_PAYLOAD_MAX];
      do {
        receive_frame(bits, frame_len, NULL);
        transmit_frame(bits + payload_len, 8);
      } while (compose_byte(bits + payload_len) != ack_num);
      output_frame(bits, payload_len);
    }
    playback_stop();
    receive_stopped = 1;
    pthread_join(receive_thread, NULL);
  } else {
    fprintf(stderr, "CSMA not implemented yet\n");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
