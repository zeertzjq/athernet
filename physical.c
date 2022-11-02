#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "backend.h"
#include "common.h"

#define PERIOD_USEC (1000000 / RATE)
#define BIT_LEN 4
#define PREAMBLE_LEN 320
#define HALF_PREAMBLE_LEN 160

int volume = 16384;
bool has_ack = false;
sig_atomic_t receive_stopped = 0;

static const int carrier[BIT_LEN] = {1, 1, -1, 1};
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

static uint8_t crc8(const bool *const bits) {
  uint8_t remainder = 0;
  for (int i = 0; i < FRAME_BITS; i++) {
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

void transmit_frame(const bool *const bits) {
  playback_len = PREAMBLE_LEN;
  for (int i = 0; i < FRAME_BITS; i++) {
    encode_bit(bits[i]);
  }
  bool crc_bits[8];
  decompose_byte(crc8(bits), crc_bits);
  for (int i = 0; i < 8; i++) {
    encode_bit(crc_bits[i]);
  }
  playback_write(playback_buf, playback_len);
}

static int16_t capture_buf[PREAMBLE_LEN * 2];
static int16_t read_pos = 0;
static int16_t read_end = 0;
static bool received_bits[FRAME_BITS];
static sig_atomic_t did_receive = 0;

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
    if (product_half > 0.3 && product_half > max_product_half) {
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

void *receive_loop(void *args) {
  bool found_preamble = false;
  int16_t bit_buf[BIT_LEN];
  size_t bit_pos = 0;
  bool bits[FRAME_BITS];
  size_t frame_pos = 0;
  bool crc_bits[8];
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
        if (frame_pos == FRAME_BITS) {
          crc_bits[crc_pos++] = decode_bit(bit_buf);
        } else {
          bits[frame_pos++] = decode_bit(bit_buf);
        }
        bit_pos = 0;
        if (frame_pos == FRAME_BITS && crc_pos == 8) {
          found_preamble = false;
          frame_pos = 0;
          crc_pos = 0;
          if (has_ack && crc8(bits) != compose_byte(crc_bits)) {
            continue;
          }
          memcpy(received_bits, bits, sizeof(received_bits));
          did_receive = 1;
          continue;
        }
      }
    }
  }
  capture_stop();
  return NULL;
}

void receive_frame(bool *const bits, suseconds_t *const timeout) {
  while (!did_receive) {
    if (timeout != NULL) {
      if (*timeout >= PERIOD_USEC) {
        *timeout -= PERIOD_USEC;
      } else {
        *timeout = -1;
        return;
      }
    }
    usleep(PERIOD_USEC);
  }
  memcpy(bits, received_bits, sizeof(received_bits));
  did_receive = 0;
}
