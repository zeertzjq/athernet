#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

#define MAC_HEADER_LEN 16

static int transmit_bytes = 0;
static int receive_bytes = 0;

static void input_frame(bool *const bits, const size_t len) {
  for (int i = 0; i < len; i += 8) {
    if (--transmit_bytes >= 0) {
      decompose_u8(getchar(), bits + i);
    }
  }
}

static void output_frame(const bool *const bits, const size_t len) {
  for (int i = 0; i < len; i += 8) {
    if (--receive_bytes >= 0) {
      putchar(compose_u8(bits + i));
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
  const size_t payload_len = has_ack ? 800 : PHY_PAYLOAD_FIXED;
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
        phy_transmit_frame(bits, payload_len);
      }
      playback_stop();
    } else if (receive_cnt > 0 && transmit_cnt == 0) {
      pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
      for (int i = 0; i < receive_cnt; i++) {
        bool bits[PHY_PAYLOAD_MAX];
        phy_receive_frame(bits, payload_len, NULL);
        output_frame(bits, payload_len);
      }
      receive_stopped = 1;
      pthread_join(receive_thread, NULL);
    }
    return EXIT_SUCCESS;
  }
  enum {
    FRAME_DATA = 0,
    FRAME_ACK = 1,
  };

  const size_t frame_len = payload_len + MAC_HEADER_LEN;
  pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
  playback_start();

  if (transmit_cnt > 0 && receive_cnt == 0) {
    for (int i = 0; i < transmit_cnt; i++) {
      const uint16_t data_header = (FRAME_DATA << 4) | (i & 0xF);
      const uint16_t ack_header = (FRAME_ACK << 4) | (i & 0xF);
      bool bits[PHY_PAYLOAD_MAX];
      decompose_u16(data_header, bits);
      input_frame(bits + MAC_HEADER_LEN, payload_len);
      int num_retries = 5;
      do {
        phy_transmit_frame(bits, frame_len);
        suseconds_t timeout = 50000;
        bool ack_bits[MAC_HEADER_LEN];
        phy_receive_frame(ack_bits, MAC_HEADER_LEN, &timeout);
        if (timeout >= 0 && compose_u16(ack_bits) == ack_header) {
          break;
        }
      } while (--num_retries >= 0);
      if (num_retries < 0) {
        fprintf(stderr, "link error\n");
        break;
      }
    }
  } else if (receive_cnt > 0 && transmit_cnt == 0) {
    for (int i = 0; i < receive_cnt; i++) {
      bool bits[PHY_PAYLOAD_MAX];
      for (;;) {
        phy_receive_frame(bits, frame_len, NULL);
        const uint16_t data_header = compose_u16(bits);
        const uint16_t ack_header = (FRAME_ACK << 4) | (data_header & 0xF);
        bool ack_bits[MAC_HEADER_LEN];
        decompose_u16(ack_header, ack_bits);
        phy_transmit_frame(ack_bits, MAC_HEADER_LEN);
        if (data_header == ((FRAME_DATA << 4) | (i & 0xF))) {
          break;
        }
      }
      output_frame(bits + MAC_HEADER_LEN, payload_len);
    }
  } else {
    fprintf(stderr, "CSMA not implemented yet\n");
    return EXIT_FAILURE;
  }

  receive_stopped = 1;
  playback_stop();
  pthread_join(receive_thread, NULL);

  return EXIT_SUCCESS;
}
