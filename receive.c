#include <alsa/asoundlib.h>
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

#define RATE 48000
#define BIT_LEN 48
#define FRAME_BITS 100
#define PREAMBLE_LEN 480
#define LEN(s) (sizeof(s) - 1)
#define S_LEN(s) (s), LEN(s)

#define E(fn, pcm, ...)                                                        \
  do {                                                                         \
    int err_ = fn(pcm, ##__VA_ARGS__);                                         \
    if (err_ < 0) {                                                            \
      fprintf(stderr, "%s %s error: %s\n", #fn, #pcm, snd_strerror(err_));     \
    }                                                                          \
  } while (0)

static const char *device = "default";
static double carrier[RATE];
static size_t carrier_pos = 0;
static double preamble[PREAMBLE_LEN];
static snd_pcm_t *capture;
static int16_t capture_buf[PREAMBLE_LEN * 2];
static sig_atomic_t capture_pos = 0;
static sig_atomic_t stopped = 0;

static void capture_start(void) {
#if 0
  return;
#endif
  E(snd_pcm_open, &capture, device, SND_PCM_STREAM_CAPTURE, 0);
  E(snd_pcm_set_params, capture, SND_PCM_FORMAT_S16_LE,
    SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 15000);
}

static void capture_stop(void) {
#if 0
  return;
#endif
  E(snd_pcm_drain, capture);
  E(snd_pcm_close, capture);
}

static void capture_read(int16_t *buf, size_t len) {
#if 0
  for (int i = 0; i < len; i++) {
    scanf("%hd", &buf[i]);
  }
  return;
#endif
  E(snd_pcm_readi, capture, buf, len);
}

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
  static double max_product = 0;
  static size_t preamble_pos = 0;
  while (*startp != end) {
    buf_sum -= sqr(buf[0]);
    memmove(buf, buf + 1, sizeof(buf) - sizeof(buf[0]));
    buf_sum += sqr(buf[PREAMBLE_LEN - 1] = capture_buf[(*startp)++]);
    if (*startp == PREAMBLE_LEN * 2) {
      *startp = 0;
    }
    double product = 0;
    for (int i = 0; i < PREAMBLE_LEN; i++) {
      product += pow(buf[i] * preamble[i], 2);
    }
    product /= buf_sum;
    if (product > max_product) {
      max_product = product;
      preamble_pos = 0;
    } else if (max_product > 0.9) {
      preamble_pos++;
    }
    if (preamble_pos == PREAMBLE_LEN) {
      memset(buf, 0, sizeof(buf));
      buf_sum = 0;
      max_product = 0;
      preamble_pos = 0;
      return true;
    }
  }
  return false;
}

static size_t remaining(size_t start, size_t end) {
  return end - start + (start < end ? 0 : PREAMBLE_LEN * 2);
}

static bool decode_bit(size_t *startp, size_t end) {
  double product = 0;
  for (int i = 0; i < BIT_LEN; i++) {
    product += capture_buf[(*startp)++] * carrier[carrier_pos++];
    if (*startp == PREAMBLE_LEN * 2) {
      *startp = 0;
    }
    if (carrier_pos == RATE) {
      carrier_pos = 0;
    }
  }
  return product > 0;
}

int main(int argc, char **argv) {
  for (int i = 0; i < RATE; i++) {
    double t = i / (double)RATE;
    carrier[i] = cos(2 * M_PI * 10000 * t);
  }
  for (int i = 0; i < PREAMBLE_LEN; i++) {
    double t = i / (double)RATE;
    preamble[i] = cos(2 * M_PI * 12000 * t);
  }
  pthread_t capture_thread;
  pthread_create(&capture_thread, NULL, capture_loop, NULL);
  size_t capture_read_pos = 0;
  bool found_preamble = false;
  size_t frame_pos = 0;
  for (;;) {
    size_t capture_read_end = capture_pos;
    if (capture_read_pos == capture_read_end) {
      usleep(100);
      continue;
    }
    if (!found_preamble) {
      found_preamble = find_preamble(&capture_read_pos, capture_read_end);
      continue;
    }
    while (remaining(capture_read_pos, capture_read_end) >= 48) {
      printf("%d", decode_bit(&capture_read_pos, capture_read_end));
      if (++frame_pos == FRAME_BITS) {
        found_preamble = false;
        frame_pos = 0;
        putchar('\n');
        break;
      }
    }
  }
  pthread_join(capture_thread, NULL);
  return EXIT_SUCCESS;
}
