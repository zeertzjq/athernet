#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

#define MAC_HEADER_LEN 16
#define MAC_HEADER(dest, src, type, seq)                                       \
  (((dest) << 12) | ((src) << 8) | ((type) << 4) | (seq))

static int transmit_bytes = 0;
static int receive_bytes = 0;
static int addr_self = 7;
static int addr_other = 13;

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
  size_t payload_len = has_ack ? 800 : PHY_PAYLOAD_FIXED;
  const int transmit_cnt = (transmit_bytes * 8 + payload_len - 1) / payload_len;
  const int receive_cnt = (receive_bytes * 8 + payload_len - 1) / payload_len;

  phy_init();
  pthread_t receive_thread;

  if (!has_ack) {
    if (transmit_cnt > 0 && receive_cnt == 0) {
      playback_start();
      for (int i = 0; i < transmit_cnt; i++) {
        bool bits[PHY_PAYLOAD_FIXED];
        input_frame(bits, PHY_PAYLOAD_FIXED);
        phy_transmit_frame(bits, PHY_PAYLOAD_FIXED);
      }
      playback_stop();
    } else if (receive_cnt > 0 && transmit_cnt == 0) {
      pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
      for (int i = 0; i < receive_cnt; i++) {
        bool bits[PHY_PAYLOAD_FIXED];
        phy_receive_frame(bits, PHY_PAYLOAD_FIXED, NULL);
        output_frame(bits, PHY_PAYLOAD_FIXED);
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

  pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
  playback_start();

  if (transmit_cnt > 0 && receive_cnt == 0) {
    for (int i = 0; i <= transmit_cnt; i++) {
      if (transmit_bytes * 8 < payload_len) {
        payload_len = transmit_bytes * 8;
      }
      const uint16_t data_header =
          MAC_HEADER(addr_other, addr_self, FRAME_DATA, i & 0xF);
      const uint16_t ack_header_want =
          MAC_HEADER(addr_self, addr_other, FRAME_ACK, i & 0xF);
      bool bits[PHY_PAYLOAD_MAX];
      decompose_u16(data_header, bits);
      input_frame(bits + MAC_HEADER_LEN, payload_len);
      int num_retries = 5;
      do {
        phy_transmit_frame(bits, MAC_HEADER_LEN + payload_len);
        suseconds_t timeout = 50000;
        bool ack_bits[MAC_HEADER_LEN];
        do {
          phy_receive_frame(ack_bits, MAC_HEADER_LEN, &timeout);
        } while (timeout >= 0 && compose_u16(ack_bits) != ack_header_want);
        if (timeout >= 0) {
          break;
        }
      } while (--num_retries >= 0);
      if (num_retries < 0 && payload_len > 0) {
        fprintf(stderr, "link error\n");
        break;
      }
    }
  } else if (receive_cnt > 0 && transmit_cnt == 0) {
    for (int i = 0; i <= receive_cnt; i++) {
      const uint16_t data_header_want =
          MAC_HEADER(addr_self, addr_other, FRAME_DATA, i & 0xF);
      bool bits[PHY_PAYLOAD_MAX];
      for (;;) {
        uint16_t data_header_got;
        do {
          const size_t len = phy_receive_frame(bits, PHY_PAYLOAD_MAX, NULL);
          payload_len = len - MAC_HEADER_LEN;
          data_header_got = compose_u16(bits);
        } while ((data_header_got & 0xFFF0) != (data_header_want & 0xFFF0));
        const uint16_t ack_header =
            MAC_HEADER(addr_other, addr_self, FRAME_ACK, data_header_got & 0xF);
        bool ack_bits[MAC_HEADER_LEN];
        decompose_u16(ack_header, ack_bits);
        phy_transmit_frame(ack_bits, MAC_HEADER_LEN);
        if (data_header_got == data_header_want) {
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
