#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

#define PERIOD_NS (1000000000L / RATE)
#define BIT_LEN 3
#define LEN_BITS 16
#define CRC_BITS 8
#define PREAMBLE_LEN 160
#define HALF_PREAMBLE_LEN 80

int volume = 16384;
bool has_ack = true;
volatile sig_atomic_t receive_stopped = 0;

static const int carrier[BIT_LEN] = {1, 1, -1};
static double preamble[PREAMBLE_LEN];
static int16_t playback_buf[RATE];
static size_t playback_len = 0;

void phy_init(void) {
  for (int i = 0; i < HALF_PREAMBLE_LEN; i++) {
    const double t = i / 24. + i * i / 2880.;
    preamble[PREAMBLE_LEN - 1 - i] = preamble[i] = cos(2 * M_PI * t);
  }
  for (int i = 0; i < PREAMBLE_LEN; i++) {
    playback_buf[i] = preamble[i] * volume;
  }
}

static uint8_t crc8(const bool *const bits, const size_t len) {
  uint8_t remainder = 0;
  for (int i = 0; i < len; i++) {
    if (remainder & 0x80) {
      remainder = (remainder << 1) ^ 0x39;
    } else {
      remainder <<= 1;
    }
    remainder ^= bits[i];
  }
  return remainder;
}

static void encode_bit(const bool bit) {
  for (int i = 0; i < BIT_LEN; i++) {
    playback_buf[playback_len++] = (bit ? 1 : -1) * volume * carrier[i];
  }
}

void phy_transmit_frame(const bool *const bits, const size_t len) {
  playback_len = PREAMBLE_LEN;
  if (has_ack) {
    bool len_bits[LEN_BITS];
    decompose_u16(len, len_bits);
    for (int i = 0; i < LEN_BITS; i++) {
      encode_bit(len_bits[i]);
    }
  }
  for (int i = 0; i < len; i++) {
    encode_bit(bits[i]);
  }
  bool crc_bits[CRC_BITS];
  decompose_u8(crc8(bits, len), crc_bits);
  for (int i = 0; i < CRC_BITS; i++) {
    encode_bit(crc_bits[i]);
  }
  playback_write(playback_buf, playback_len);
}

static int16_t capture_buf[PREAMBLE_LEN * 2];
static size_t read_pos = 0;
static size_t read_end = 0;
static bool received_bits[PHY_PAYLOAD_MAX];
static volatile sig_atomic_t received_len = -1;

static int64_t sqr(const int64_t x) { return x * x; }

static bool find_preamble(void) {
  static int16_t buf[PREAMBLE_LEN];
  static int64_t buf_abs_sum = 0;
  static int64_t buf_sqr_sum = 0;
  static double max_product_half = 0;
  static int preamble_pos = -1;
  while (read_pos != read_end) {
    buf_abs_sum -= abs(buf[0]);
    buf_sqr_sum -= sqr(buf[0]);
    memmove(buf, buf + 1, sizeof(buf) - sizeof(buf[0]));
    buf[PREAMBLE_LEN - 1] = capture_buf[read_pos++];
    buf_abs_sum += abs(buf[PREAMBLE_LEN - 1]);
    buf_sqr_sum += sqr(buf[PREAMBLE_LEN - 1]);
    if (read_pos == PREAMBLE_LEN * 2) {
      read_pos = 0;
    }
    double product_half = 0;
    for (int i = 0; i < HALF_PREAMBLE_LEN; i++) {
      product_half += preamble[i] * buf[HALF_PREAMBLE_LEN + i];
    }
    product_half /= (double)buf_sqr_sum / buf_abs_sum * PREAMBLE_LEN / 2;
    if (product_half > 0.4 && product_half > max_product_half) {
      max_product_half = product_half;
      preamble_pos = 0;
    } else if (preamble_pos >= 0) {
      preamble_pos++;
    }
    if (preamble_pos == HALF_PREAMBLE_LEN) {
      memset(buf, 0, sizeof(buf));
      buf_abs_sum = 0;
      buf_sqr_sum = 0;
      max_product_half = 0;
      preamble_pos = -1;
      return true;
    }
  }
  return false;
}

static bool decode_bit(const int16_t *const buf) {
  int product = 0;
  for (int i = 0; i < BIT_LEN; i++) {
    product += buf[i] * carrier[i];
  }
  return product > 0;
}

void *phy_receive_loop(void *args) {
  bool found_preamble = false;
  int16_t bit_buf[BIT_LEN];
  size_t bit_pos = 0;
  bool len_bits[LEN_BITS];
  size_t len_pos = 0;
  size_t payload_len = PHY_PAYLOAD_FIXED;
  bool bits[PHY_PAYLOAD_MAX];
  size_t payload_pos = 0;
  bool crc_bits[CRC_BITS];
  size_t crc_pos = 0;
  capture_start();
  while (!receive_stopped) {
    capture_read(capture_buf + read_end, PREAMBLE_LEN);
    read_end = PREAMBLE_LEN - read_end;
    while (read_pos != read_end) {
      if (!found_preamble) {
        found_preamble = find_preamble();
        continue;
      }
      bit_buf[bit_pos++] = capture_buf[read_pos++];
      if (read_pos == PREAMBLE_LEN * 2) {
        read_pos = 0;
      }
      if (bit_pos == BIT_LEN) {
        bit_pos = 0;
        if (has_ack && len_pos < LEN_BITS) {
          len_bits[len_pos++] = decode_bit(bit_buf);
          if (len_pos == LEN_BITS) {
            const size_t len = compose_u16(len_bits);
            if (len > PHY_PAYLOAD_MAX) {
              found_preamble = false;
              len_pos = 0;
            } else {
              payload_len = len;
            }
          }
          continue;
        }
        if (payload_pos == payload_len) {
          crc_bits[crc_pos++] = decode_bit(bit_buf);
        } else {
          bits[payload_pos++] = decode_bit(bit_buf);
        }
        if (payload_pos == payload_len && crc_pos == CRC_BITS) {
          found_preamble = false;
          len_pos = 0;
          payload_pos = 0;
          crc_pos = 0;
          if (has_ack && crc8(bits, payload_len) != compose_u8(crc_bits)) {
            continue;
          }
          memcpy(received_bits, bits, payload_len * sizeof(bool));
          received_len = payload_len;
          continue;
        }
      }
    }
  }
  capture_stop();
  return NULL;
}

size_t phy_receive_frame(bool *const bits, const size_t max_len,
                         long *const timeout_ns) {
  size_t frame_len = received_len;
  while (frame_len < 0 || frame_len > max_len) {
    if (timeout_ns != NULL) {
      if (*timeout_ns >= PERIOD_NS) {
        *timeout_ns -= PERIOD_NS;
      } else {
        *timeout_ns = -1;
        return 0;
      }
    }
    const struct timespec sleep_time = {
        .tv_nsec = PERIOD_NS,
    };
    nanosleep(&sleep_time, NULL);
    frame_len = received_len;
  }
  received_len = -1;
  memcpy(bits, received_bits, frame_len * sizeof(bool));
  return frame_len;
}
