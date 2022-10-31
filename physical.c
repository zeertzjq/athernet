#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backend.h"
#include "common.h"

#define PERIOD_USEC (1000000 / RATE)
#define BIT_LEN 48
#define PREAMBLE_LEN 480
#define HALF_PREAMBLE_LEN 240

int carrier_freq = 10000;
int volume = 16384;
sig_atomic_t capture_stopped = 0;

static double carrier[RATE];
static double preamble[PREAMBLE_LEN];
static int16_t playback_buf[RATE];
static size_t playback_len = 0;
static int16_t capture_buf[PREAMBLE_LEN * 2];
static sig_atomic_t capture_pos = 0;

void *capture_loop(void *args) {
  capture_start();
  while (!capture_stopped) {
    size_t pos = capture_pos;
    capture_read(capture_buf + pos, PREAMBLE_LEN);
    capture_pos = pos == PREAMBLE_LEN ? 0 : PREAMBLE_LEN;
  }
  capture_stop();
  return NULL;
}

void phy_init(void) {
  for (int i = 0; i < RATE; i++) {
    double t = i / (double)RATE;
    carrier[i] = sin(2 * M_PI * carrier_freq * t);
  }
  for (int i = 0; i < HALF_PREAMBLE_LEN; i++) {
    double t = i / 24. + i * i / 2880.;
    preamble[PREAMBLE_LEN - 1 - i] = preamble[i] = cos(2 * M_PI * t);
  }
  for (int i = 0; i < PREAMBLE_LEN; i++) {
    playback_buf[i] = preamble[i] * volume;
  }
}

static void encode_bit(bool bit, size_t *carrier_pos) {
  for (int i = 0; i < BIT_LEN; i++) {
    playback_buf[playback_len++] =
        (bit ? 1 : -1) * volume * carrier[(*carrier_pos)++];
  }
}

void transmit_frame(bool *bits) {
  playback_len = PREAMBLE_LEN;
  size_t carrier_pos = 0;
  for (int i = 0; i < FRAME_BITS; i++) {
    encode_bit(bits[i], &carrier_pos);
  }
  playback_write(playback_buf, playback_len);
}

static int64_t sqr(int64_t x) { return x * x; }

static bool find_preamble(size_t *startp, size_t end) {
  static int16_t buf[PREAMBLE_LEN];
  static int64_t buf_abs_sum = 0;
  static int64_t buf_sqr_sum = 0;
  static double max_product_half = 0;
  static int preamble_pos = -1;
  while (*startp != end) {
    buf_abs_sum -= abs(buf[0]);
    buf_sqr_sum -= sqr(buf[0]);
    memmove(buf, buf + 1, sizeof(buf) - sizeof(buf[0]));
    buf[PREAMBLE_LEN - 1] = capture_buf[(*startp)++];
    buf_abs_sum += abs(buf[PREAMBLE_LEN - 1]);
    buf_sqr_sum += sqr(buf[PREAMBLE_LEN - 1]);
    if (*startp == PREAMBLE_LEN * 2) {
      *startp = 0;
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

static size_t remaining(size_t start, size_t end) {
  return end - start + (start <= end ? 0 : PREAMBLE_LEN * 2);
}

static bool decode_bit(size_t *startp, size_t *carrier_pos) {
  double product = 0;
  for (int i = 0; i < BIT_LEN; i++) {
    product += capture_buf[(*startp)++] * carrier[(*carrier_pos)++];
    if (*startp == PREAMBLE_LEN * 2) {
      *startp = 0;
    }
  }
  return product > 0;
}

bool receive_frame(bool *bits, suseconds_t timeout) {
  static size_t capture_read_pos = 0;
  bool found_preamble = false;
  size_t carrier_pos = 0;
  size_t frame_pos = 0;
  for (;;) {
    size_t capture_read_end = capture_pos;
    if (capture_read_pos == capture_read_end) {
      if (timeout >= 0) {
        if (timeout >= PERIOD_USEC) {
          timeout -= PERIOD_USEC;
        } else {
          return false;
        }
      }
      usleep(PERIOD_USEC);
      continue;
    }
    if (!found_preamble) {
      found_preamble = find_preamble(&capture_read_pos, capture_read_end);
      continue;
    }
    while (remaining(capture_read_pos, capture_read_end) >= BIT_LEN) {
      bits[frame_pos++] = decode_bit(&capture_read_pos, &carrier_pos);
      if (frame_pos == FRAME_BITS) {
        frame_pos = 0;
        return true;
      }
    }
  }
}
