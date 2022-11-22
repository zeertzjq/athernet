#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

static enum {
  NODE_DATA,
  NODE_PING,
} node_type = NODE_DATA;

static size_t input_frame(bool *const bits, const size_t max_len) {
  switch (node_type) {
  case NODE_DATA:
    for (size_t pos = 0; pos < max_len; pos += 8) {
      int c = getchar();
      if (c == EOF) {
        return pos;
      }
      decompose_u8(c, bits + pos);
    }
    return max_len;
  case NODE_PING:
    return 0;
  }
}

static void output_frame(const bool *const bits, const size_t len) {
  if (node_type != NODE_DATA) {
    return;
  }
  for (size_t pos = 0; pos < len; pos += 8) {
    putchar(compose_u8(bits + pos));
  }
  fflush(stdout);
}

#define MAC_HEADER_LEN 16
#define MAC_HEADER(dest, src, type, seq)                                       \
  (((dest) << 12) | ((src) << 8) | ((type) << 4) | (seq))

static int mac_self = 2;
static int mac_other = 1;

enum {
  FRAME_DATA = 0,
  FRAME_ACK = 1,
};

static int transmit_seq = 0;
static bool transmit_bits[PHY_PAYLOAD_MAX];
static size_t transmit_len = 0;
static uint16_t ack_header_want = 0;
static int num_retries = 0;
static int64_t time_transmit_start = 0;
static int64_t time_transmit_end = 0;

static void mac_transmit_prepare(void) {
  num_retries = node_type == NODE_PING ? 1 : 8;
  const uint16_t data_header =
      MAC_HEADER(mac_other, mac_self, FRAME_DATA, transmit_seq);
  decompose_u16(data_header, transmit_bits);
  transmit_len = input_frame(transmit_bits + MAC_HEADER_LEN, 800);
  ack_header_want = MAC_HEADER(mac_self, mac_other, FRAME_ACK, transmit_seq);
}

static void mac_transmit_retry(void) {
  if (node_type == NODE_PING) {
    time_transmit_start = time_ns();
  }
  phy_transmit_frame(transmit_bits, MAC_HEADER_LEN + transmit_len);
  time_transmit_end = time_ns();
}

static void mac_send_ack(const int seq) {
  const uint16_t ack_header = MAC_HEADER(mac_other, mac_self, FRAME_ACK, seq);
  bool ack_bits[MAC_HEADER_LEN];
  decompose_u16(ack_header, ack_bits);
  phy_transmit_frame(ack_bits, MAC_HEADER_LEN);
}

int main(int argc, char **argv) {
  bool transmit = false;
  bool receive = false;

  if (argc <= 1) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "send") == 0) {
    transmit = true;
  } else if (strcmp(argv[1], "recv") == 0) {
    receive = true;
  } else if (strcmp(argv[1], "ping") == 0) {
    node_type = NODE_PING;
    transmit = true;
    receive = true;
  } else {
    fprintf(stderr, "Invalid argument: %s\n", argv[1]);
    return EXIT_FAILURE;
  }

  if (transmit && argc <= 2) {
    fprintf(stderr, "Missing argument\n");
    return EXIT_FAILURE;
  }

  for (int i = 2; i < argc; i++) {
    if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
      volume = atoi(argv[i] + LEN("--volume="));
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }

  phy_init();
  pthread_t receive_thread;
  pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
  playback_start();

  const int64_t ack_timeout = node_type == NODE_PING ? 2000000000 : 100000000;

  if (transmit) {
    mac_transmit_prepare();
    mac_transmit_retry();
  }

  const uint16_t receive_ack_header =
      MAC_HEADER(mac_self, mac_other, FRAME_ACK, 0);
  const uint16_t receive_data_header =
      MAC_HEADER(mac_self, mac_other, FRAME_DATA, 0);

  bool receive_end = false;
  int64_t receive_end_time = 0;
  const int64_t end_timeout = 200000000;

  while (transmit || receive) {
    int64_t poll_timeout = 10000000;
    if (phy_poll_frame(&poll_timeout)) {
      bool bits[PHY_PAYLOAD_MAX];
      const size_t len = phy_receive_frame(bits) - MAC_HEADER_LEN;
      const uint16_t header = compose_u16(bits);
      if (receive && (header & 0xFFF0) == receive_data_header) {
        const int seq = header & 0xF;
        mac_send_ack(seq);
        if (node_type == NODE_DATA) {
          static int receive_seq = 0;
          if (seq == receive_seq) {
            output_frame(bits + MAC_HEADER_LEN, len);
            if (len == 0) {
              receive_end = true;
              receive_end_time = time_ns();
            } else {
              receive_seq = (receive_seq + 1) & 0xF;
            }
          }
        }
      } else if (transmit && header == ack_header_want) {
        if (transmit_len == 0 && node_type == NODE_DATA) {
          transmit = false;
        } else {
          if (node_type == NODE_PING) {
            fprintf(stderr, "%lf ms\n",
                    (time_ns() - time_transmit_start) / 1e6);
          }
          transmit_seq = (transmit_seq + 1) & 0xF;
          mac_transmit_prepare();
          mac_transmit_retry();
        }
      }
      continue;
    }

    const int64_t time_now = time_ns();

    if (receive && receive_end && time_now - receive_end_time > end_timeout) {
      receive = false;
    }

    if (transmit && time_now - time_transmit_end > ack_timeout) {
      if (num_retries == 0) {
        if (node_type == NODE_DATA) {
          fprintf(stderr, "link error\n");
          transmit = false;
        } else {
          if (node_type == NODE_PING) {
            fprintf(stderr, "TIMEOUT\n");
          }
          mac_transmit_prepare();
          mac_transmit_retry();
        }
      } else {
        mac_transmit_retry();
      }
    }
  }

  phy_receive_stopped = 1;
  playback_stop();
  pthread_join(receive_thread, NULL);

  return EXIT_SUCCESS;
}
