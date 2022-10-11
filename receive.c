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
static int16_t preamble[PREAMBLE_LEN];
static int16_t capture_buf[PREAMBLE_LEN * 2];
static sig_atomic_t capture_pos = 0;
static sig_atomic_t stopped = 0;
static int volume = 2000;

static void *capture_loop(void *args) {
  snd_pcm_t *capture;
  E(snd_pcm_open, &capture, device, SND_PCM_STREAM_CAPTURE, 0);
  E(snd_pcm_set_params, capture, SND_PCM_FORMAT_S16_LE,
    SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 15000);
  while (!stopped) {
    size_t pos = capture_pos;
    E(snd_pcm_readi, capture, capture_buf + pos, PREAMBLE_LEN);
    capture_pos = pos == PREAMBLE_LEN ? 0 : PREAMBLE_LEN;
  }
  E(snd_pcm_drain, capture);
  E(snd_pcm_close, capture);
  return NULL;
}

static bool find_preamble(size_t *startp, size_t end) {
  static int16_t buf[PREAMBLE_LEN];
  static int32_t buf_sum = 0;
  static int64_t max_product = 0;
  static size_t preamble_pos = 0; 
  while (*startp < end) {
    buf_sum -= buf[0];
    memmove(buf, buf + 1, sizeof(buf) - sizeof(buf[0]));
    buf_sum += (buf[PREAMBLE_LEN - 1] = capture_buf[(*startp)++]);
    int64_t product = 0;
    for (int i = 0; i < PREAMBLE_LEN; i++) {
      product += buf[i] + preamble[i];
    }
    if (product > max_product && product > buf_sum / PREAMBLE_LEN * 2) {
      max_product = product;
      preamble_pos = 0;
    } else {
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

int main(int argc, char **argv) {
  for (int i = 0; i < RATE; i++) {
    double t = i / (double)RATE;
    carrier[i] = sin(2 * M_PI * 10000 * t);
  }
  for (int i = 0; i < PREAMBLE_LEN; i++) {
    double t = i / (double)RATE;
    preamble[i] = sin(2 * M_PI * 12000 * t) * volume;
  }
  pthread_t capture_thread;
  pthread_create(&capture_thread, NULL, capture_loop, NULL);
  size_t capture_read_pos = 0;
  bool found_preamble = false;
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
  }
  return EXIT_SUCCESS;
}
