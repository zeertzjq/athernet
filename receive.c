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

#include "util.c"
#include "conf.h"
#include "crc-lib-c/crcLib.h"

#include "backend.h"
#include "constants.h"

#define LEN(s) (sizeof(s) - 1)
#define S_LEN(s) (s), LEN(s)

static const char *device = "default";
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
  static int64_t buf_sum = 0;
  static double max_product_full = 0;
  static double max_product_half = 0;
  static size_t preamble_pos = 0;
  while (*startp != end) {
    buf_sum -= sqr(buf[0]);
    memmove(buf, buf + 1, sizeof(buf) - sizeof(buf[0]));
    buf_sum += sqr(buf[PREAMBLE_LEN - 1] = capture_buf[(*startp)++]);
    if (*startp == PREAMBLE_LEN * 2) {
      *startp = 0;
    }
    double product_full = 0;
    double product_half = 0;
    for (int i = 0; i < PREAMBLE_LEN; i++) {
      product_full += preamble[i] * buf[i];
    }
    for (int i = 0; i < HALF_PREAMBLE_LEN; i++) {
      product_half += preamble[i] * buf[HALF_PREAMBLE_LEN + i];
    }
    if (product_half > max_product_half) {
      max_product_half = product_half;
      max_product_full = product_full;
      preamble_pos = 0;
    } else if (max_product_half > 1) {
      preamble_pos++;
    }
    if (preamble_pos == HALF_PREAMBLE_LEN) {
      memset(buf, 0, sizeof(buf));
      buf_sum = 0;
      max_product_full = 0;
      max_product_half = 0;
      preamble_pos = 0;
      return true;
    }
  }
  return false;
}

void decode(size_t* buf, int* decode_power_bit){
  if (sizeof(buf)/sizeof(size_t) == 44*108){
    float decode_remove_carrier[44*108];
    /*use smooth filter and decode*/
    decode_remove_carrier = filter(m_dot2(44*108,get_carrier(), buf),10);
    for (int j=0;j<108;++j){
      decode_power_bit[j] = sum_list(decode_remove_carrier,10+j*44,30+j*44);
    }
    /*normalize the value of bit to 1 and 0*/
    for(int i=0;i<length;++i){
        if (decode_power_bit[i]>0)decode_power_bit[i]=1;
        else decode_power_bit[i]=0;
    }
  }
}

bool crc_check(int* buf){
  int pre[100]
  int behind[8];
  memcpy(pre, buf, 100);
  uint8_t crc = crc8_maxim(pre, 100)
  for(int i=7;i>=0;--i){
      behind[7-i] = (crc>>i)&1;
      if (buf[107-i]!=behind[7-i]){
        return false;
      }
  }
  return true;

}

static size_t remaining(size_t start, size_t end) {
  return end - start + (start < end ? 0 : PREAMBLE_LEN * 2);
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

int main(int argc, char **argv) {
  for (int i = 0; i < RATE; i++) {
    double t = i / (double)RATE;
    carrier[i] = cos(2 * M_PI * 10000 * t);
  }

  for (int i = 0; i < HALF_PREAMBLE_LEN; i++) {
    double tmp = i / 24. + i * i / 2880.;
    preamble[PREAMBLE_LEN - 1 - i] = preamble[i] = cos(2 * M_PI * tmp);
  }

  pthread_t capture_thread;
  pthread_create(&capture_thread, NULL, capture_loop, NULL);

  size_t capture_read_pos = 0;
  bool found_preamble = false;
  size_t frame_pos = 0;
  size_t num_frames = 0;
  int correct_frame = 0;

  for (;;) {
    if (num_frames == 100) {
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
      printf("%d", decode_bit(&capture_read_pos));
      if (++frame_pos == FRAME_BITS) {
        found_preamble = false;
        frame_pos = 0;
        putchar('\n');
        num_frames++;
        break;
      }
    }
  }

  pthread_join(capture_thread, NULL);
  return EXIT_SUCCESS;
}
