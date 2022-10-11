#include <alsa/asoundlib.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define BIT_LEN 48
#define PREAMBLE_LEN 480
#define ZERO_LEN 480
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
static int16_t bit_buf[BIT_LEN];
static int16_t preamble[PREAMBLE_LEN];
static int16_t zero_buf[ZERO_LEN];
static snd_pcm_t *playback;
static int volume = 2000;

static void transmit_bit(bool bit) {
  for (int i = 0; i < BIT_LEN; i++) {
    bit_buf[i] = carrier[carrier_pos + i] * (bit ? 1 : -1) * volume;
  }
  E(snd_pcm_writei, playback, bit_buf, BIT_LEN);
  carrier_pos += BIT_LEN;
  if (carrier_pos == RATE) {
    carrier_pos = 0;
  }
}

static void transmit_frame(bool bit) {
  E(snd_pcm_writei, playback, preamble, PREAMBLE_LEN);
  for (int i = 0; i < 100; i++) {
    transmit_bit(bit);
  }
}

int main(int argc, char **argv) {                                         
  E(snd_pcm_open, &playback, device, SND_PCM_STREAM_PLAYBACK, 0);
  E(snd_pcm_set_params, playback, SND_PCM_FORMAT_S16_LE,
    SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 15000);
  for (int i = 0; i < RATE; i++) {
    double t = i / (double)RATE;
    carrier[i] = sin(2 * M_PI * 10000 * t);
  }
  for (int i = 0; i < PREAMBLE_LEN; i++) {
    double t = i / (double)RATE;
    preamble[i] = sin(2 * M_PI * 12000 * t) * volume;
  }
  for (int i = 0; i < 100; i++) {
    E(snd_pcm_writei, playback, zero_buf, ZERO_LEN);
    transmit_frame(1);
  }
  E(snd_pcm_drain, playback);
  E(snd_pcm_close, playback);
  return EXIT_SUCCESS;
}
