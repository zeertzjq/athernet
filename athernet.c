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

static size_t input_frame(bool *const bits, const size_t max_len) {
  for (size_t pos = 0; pos < max_len; pos += 8) {
    int c = getchar();
    if (c == EOF) {
      return pos;
    }
    decompose_u8(c, bits + pos);
  }
  return max_len;
}

static void output_frame(const bool *const bits, const size_t len) {
  for (size_t pos = 0; pos < len; pos += 8) {
    putchar(compose_u8(bits + pos));
  }
  fflush(stdout);
}

static int addr_self = 0;
static int addr_other = 0;

enum {
  FRAME_DATA = 0,
  FRAME_ACK = 1,
};

static size_t mac_transmit_frame(const int frame_type, const int seq) {
  const uint16_t data_header =
      MAC_HEADER(addr_other, addr_self, frame_type, seq);
  const uint16_t ack_header_want =
      MAC_HEADER(addr_self, addr_other, FRAME_ACK, seq);
  bool bits[PHY_PAYLOAD_MAX];
  decompose_u16(data_header, bits);
  const size_t len = input_frame(bits + MAC_HEADER_LEN, 800);
  int num_retries = 5;
  do {
    phy_transmit_frame(bits, MAC_HEADER_LEN + len);
    suseconds_t timeout = 50000;
    bool ack_bits[MAC_HEADER_LEN];
    do {
      phy_receive_frame(ack_bits, MAC_HEADER_LEN, &timeout);
    } while (timeout >= 0 && compose_u16(ack_bits) != ack_header_want);
    if (timeout >= 0) {
      break;
    }
  } while (--num_retries >= 0);
  if (num_retries < 0 && len > 0) {
    fprintf(stderr, "link error\n");
  }
  return len;
}

static size_t mac_receive_frame(const int frame_type, const int seq) {
  const uint16_t data_header_want =
      MAC_HEADER(addr_self, addr_other, frame_type, seq);
  bool bits[PHY_PAYLOAD_MAX];
  size_t len = 0;
  for (;;) {
    uint16_t data_header_got;
    do {
      len = phy_receive_frame(bits, PHY_PAYLOAD_MAX, NULL) - MAC_HEADER_LEN;
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
  output_frame(bits + MAC_HEADER_LEN, len);
  return len;
}

int main(int argc, char **argv) {
  bool transmit = false;
  bool receive = false;

  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
      volume = atoi(argv[i] + LEN("--volume="));
    } else if (strcmp(argv[i], "--transmit") == 0) {
      transmit = true;
    } else if (strcmp(argv[i], "--receive") == 0) {
      receive = true;
    } else if (strncmp(argv[i], S_LEN("--self=")) == 0) {
      addr_self = atoi(argv[i] + LEN("--self=")) & 0xF;
    } else if (strncmp(argv[i], S_LEN("--other=")) == 0) {
      addr_other = atoi(argv[i] + LEN("--other=")) & 0xF;
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  if (!transmit && !receive) {
    fprintf(stderr, "Nothing to do\n");
    return EXIT_FAILURE;
  }

  phy_init();
  pthread_t receive_thread;
  pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
  playback_start();

  if (transmit && !receive) {
    for (int seq = 0;; seq = (seq + 1) & 0xF) {
      const size_t len = mac_transmit_frame(FRAME_DATA, seq);
      if (len == 0) {
        break;
      }
    }
  } else if (receive && !transmit) {
    for (int seq = 0;; seq = (seq + 1) & 0xF) {
      const size_t len = mac_receive_frame(FRAME_DATA, seq);
      if (len == 0) {
        break;
      }
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
