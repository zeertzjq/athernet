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
  NODE_PERF,
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
  case NODE_PERF:
    for (size_t pos = 0; pos < max_len; pos += 8) {
      decompose_u8(rand(), bits + pos);
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

static int addr_self = 0;
static int addr_other = 0;

enum {
  FRAME_DATA = 0,
  FRAME_ACK = 1,
};

static int64_t time_initial = 0;

static size_t mac_transmit_frame(const int seq) {
  const uint16_t data_header =
      MAC_HEADER(addr_other, addr_self, FRAME_DATA, seq);
  const uint16_t ack_header_want =
      MAC_HEADER(addr_self, addr_other, FRAME_ACK, seq);
  bool bits[PHY_PAYLOAD_MAX];
  decompose_u16(data_header, bits);
  const size_t len = input_frame(bits + MAC_HEADER_LEN, 800);
  int num_retries = node_type == NODE_PING ? 1 : 8;
  const int64_t time_start = node_type == NODE_PING ? time_ns() : 0;
  do {
    phy_transmit_frame(bits, MAC_HEADER_LEN + len);
    int64_t timeout_ns = node_type == NODE_PING ? 2000000000 : 100000000;
    bool ack_bits[MAC_HEADER_LEN];
    do {
      phy_receive_frame(ack_bits, MAC_HEADER_LEN, &timeout_ns);
    } while (timeout_ns >= 0 && compose_u16(ack_bits) != ack_header_want);
    if (timeout_ns >= 0) {
      break;
    }
  } while (--num_retries > 0);
  if (node_type == NODE_DATA && num_retries <= 0 && len > 0) {
    fprintf(stderr, "link error\n");
    return 0;
  }
  if (node_type == NODE_PERF && num_retries > 0) {
    static int64_t total_bits = 0;
    total_bits += len;
    fprintf(stderr, "%lf\n", total_bits / ((time_ns() - time_initial) / 1e9));
  }
  if (node_type == NODE_PING) {
    if (num_retries <= 0) {
      fprintf(stderr, "TIMEOUT\n");
    } else {
      fprintf(stderr, "%lf\n", (time_ns() - time_start) / 1e6);
    }
  }
  return len;
}

static size_t mac_receive_frame(const int seq) {
  const uint16_t data_header_want =
      MAC_HEADER(addr_self, addr_other, FRAME_DATA, seq);
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
    if (node_type != NODE_DATA || data_header_got == data_header_want) {
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
    } else if (strcmp(argv[i], "--perf") == 0) {
      node_type = NODE_PERF;
      transmit = true;
      receive = true;
    } else if (strcmp(argv[i], "--ping") == 0) {
      node_type = NODE_PING;
      transmit = true;
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
  if (node_type == NODE_PERF) {
    srand(time_initial = time_ns());
  }

  phy_init();
  pthread_t receive_thread;
  pthread_create(&receive_thread, NULL, phy_receive_loop, NULL);
  playback_start();

  for (int seq = 0; transmit || receive; seq = (seq + 1) & 0xF) {
    if (addr_self < addr_other) {
      if (receive && mac_receive_frame(seq) == 0 && node_type == NODE_DATA) {
        receive = false;
      }
      if (transmit && mac_transmit_frame(seq) == 0 && node_type == NODE_DATA) {
        transmit = false;
      }
    } else {
      if (transmit && mac_transmit_frame(seq) == 0 && node_type == NODE_DATA) {
        transmit = false;
      }
      if (receive && mac_receive_frame(seq) == 0 && node_type == NODE_DATA) {
        receive = false;
      }
    }
  }

  receive_stopped = 1;
  playback_stop();
  pthread_join(receive_thread, NULL);

  return EXIT_SUCCESS;
}
