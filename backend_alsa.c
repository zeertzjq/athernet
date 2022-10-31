#include <alsa/asoundlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"

#define E(fn, pcm, ...)                                                        \
  do {                                                                         \
    int err_ = fn(pcm, ##__VA_ARGS__);                                         \
    if (err_ < 0) {                                                            \
      fprintf(stderr, "%s %s error: %s\n", #fn, #pcm, snd_strerror(err_));     \
    }                                                                          \
  } while (0)

static const char *device = "default";
static snd_pcm_t *capture;
static snd_pcm_t *playback;

void capture_start(void) {
  E(snd_pcm_open, &capture, device, SND_PCM_STREAM_CAPTURE, 0);
  E(snd_pcm_set_params, capture, SND_PCM_FORMAT_S16_LE,
    SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 10000);
}

void capture_stop(void) {
  E(snd_pcm_drain, capture);
  E(snd_pcm_close, capture);
}

void capture_read(int16_t *buf, size_t len) {
  E(snd_pcm_readi, capture, buf, len);
}

void playback_start(void) {
  E(snd_pcm_open, &playback, device, SND_PCM_STREAM_PLAYBACK, 0);
  E(snd_pcm_set_params, playback, SND_PCM_FORMAT_S16_LE,
    SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 10000);
}

void playback_stop(void) {
  E(snd_pcm_drain, playback);
  E(snd_pcm_close, playback);
}

void playback_write(int16_t *buf, size_t len) {
  E(snd_pcm_writei, playback, buf, len);
  E(snd_pcm_drain, playback);
  E(snd_pcm_prepare, playback);
}
