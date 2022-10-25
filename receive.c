#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "backend.h"
#include "common.h"

static double carrier[RATE];
static size_t carrier_pos = 0;
static double preamble[PREAMBLE_LEN];
static int16_t capture_buf[PREAMBLE_LEN * 2];
static sig_atomic_t capture_pos = 0;
static sig_atomic_t stopped = 0;

static void *capture_loop(void *args) {
  capture_start();
  while (!stopped) {
    size_t pos = capture_pos;
    capture_read(capture_buf + pos, PREAMBLE_LEN);
    capture_pos = pos == PREAMBLE_LEN ? 0 : PREAMBLE_LEN;
  }
  capture_stop();
  return NULL;
}

static int64_t sqr(int64_t x) { return x * x; }

static bool find_preamble(size_t *startp, size_t end) {
  static int16_t buf[PREAMBLE_LEN];
  static int64_t buf_abs_sum = 0;
  static int64_t buf_sqr_sum = 0;
  static double max_product_full = 0;
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
    double product_full = 0;
    double product_half = 0;
    for (int i = 0; i < PREAMBLE_LEN; i++) {
      product_full += preamble[i] * buf[i];
    }
    product_full /= (double)buf_sqr_sum / buf_abs_sum * PREAMBLE_LEN;
    if (product_full > 0.3 && product_full > max_product_full) {
      max_product_full = product_full;
    }
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
      bool found = product_full >= max_product_full;
      memset(buf, 0, sizeof(buf));
      buf_abs_sum = 0;
      buf_sqr_sum = 0;
      max_product_full = 0;
      max_product_half = 0;
      preamble_pos = -1;
      if (found) {
        return true;
      }
    }
  }
  return false;
}

static size_t remaining(size_t start, size_t end) {
  return end - start + (start <= end ? 0 : PREAMBLE_LEN * 2);
}

static bool decode_bit(size_t *startp) {
  double product = 0;
  for (int i = 0; i < BIT_LEN; i++) {
    product += capture_buf[(*startp)++] * carrier[carrier_pos + i];
    if (*startp == PREAMBLE_LEN * 2) {
      *startp = 0;
    }
  }
  carrier_pos += BIT_LEN;
  if (carrier_pos == RATE) {
    carrier_pos = 0;
  }
  return product > 0;
}

static bool receive_frame_plain(bool new_bit, bool *bits) {
  static size_t frame_pos = 0;
  bits[frame_pos++] = new_bit;
  if (frame_pos == FRAME_BITS) {
    frame_pos = 0;
    return true;
  }
  return false;
}

static bool receive_frame_hamming(bool new_bit, bool *bits) {
  static size_t frame_pos = 0;
  static size_t src_pos = 0;
  static bool src_bits[8];
  src_bits[src_pos++] = new_bit;
  if (src_pos <= 7) {
    return false;
  }
  src_pos = 0;
  for (int j = 1; j <= 7; j++) {
    src_bits[0] ^= src_bits[j];
  }
  if (src_bits[0]) {
    size_t wrong_pos = 0;
    wrong_pos += (src_bits[1] ^ src_bits[3] ^ src_bits[5] ^ src_bits[7]) * 1;
    wrong_pos += (src_bits[2] ^ src_bits[3] ^ src_bits[6] ^ src_bits[7]) * 2;
    wrong_pos += (src_bits[4] ^ src_bits[5] ^ src_bits[6] ^ src_bits[7]) * 4;
    src_bits[wrong_pos] ^= 1;
  }
  bits[frame_pos++] = src_bits[3];
  bits[frame_pos++] = src_bits[5];
  bits[frame_pos++] = src_bits[6];
  bits[frame_pos++] = src_bits[7];
  if (frame_pos == FRAME_BITS / 2) {
    frame_pos = 0;
    return true;
  }
  return false;
}

int main(int argc, char **argv) {
  bool binary = false;
  bool (*receive_frame)(bool, bool *) = receive_frame_plain;
  int max_frames = 10000 / FRAME_BITS;
  size_t frame_bits = FRAME_BITS;
  int carrier_freq = 10000;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--binary") == 0) {
      binary = true;
    } else if (strcmp(argv[i], "--hamming") == 0) {
      receive_frame = receive_frame_hamming;
    } else if (strncmp(argv[i], S_LEN("--frames=")) == 0) {
      max_frames = atoi(argv[i] + LEN("--frames="));
    } else if (strncmp(argv[i], S_LEN("--carrier=")) == 0) {
      carrier_freq = atoi(argv[i] + LEN("--carrier="));
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }
  if (receive_frame == receive_frame_hamming) {
    max_frames *= 2;
    frame_bits /= 2;
  }

  GET_CARRIER(carrier, carrier_freq);
  GET_PREAMBLE(preamble, 1);

  pthread_t capture_thread;
  pthread_create(&capture_thread, NULL, capture_loop, NULL);

  size_t capture_read_pos = 0;
  bool found_preamble = false;
  int num_frames = 0;
  bool bits[FRAME_BITS];

  for (;;) {
    if (num_frames == max_frames) {
      stopped = 1;
      break;
    }
    size_t capture_read_end = capture_pos;
    if (capture_read_pos == capture_read_end) {
      usleep(100);
      continue;
    }
    if (!found_preamble) {
      found_preamble = find_preamble(&capture_read_pos, capture_read_end);
      continue;
    }
    while (remaining(capture_read_pos, capture_read_end) >= BIT_LEN) {
      if (receive_frame(decode_bit(&capture_read_pos), bits)) {
        found_preamble = false;
        num_frames++;
        if (binary) {
          for (int i = 0; i < frame_bits; i += 8) {
            int c = 0;
            for (int k = 0; k < 8; k++) {
              c |= bits[i + k] << k;
            }
            putchar(c);
          }
        } else {
          for (int i = 0; i < frame_bits; i++) {
            printf("%d", bits[i]);
          }
          if (isatty(STDOUT_FILENO)) {
            putchar('\n');
          }
        }
        break;
      }
    }
  }

  pthread_join(capture_thread, NULL);

  return EXIT_SUCCESS;
}
