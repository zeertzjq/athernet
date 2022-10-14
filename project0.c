// To compile:
//   gcc project0.c -lasound -lm -oproject0
//
// To record sound and replay it:
//   ./project0 --record
//
// To record sound with background sound and replay it:
//   ./project0 --record --play
//
// To play f(t) = sin(2pi 1000 t) + sin(2pi 10000 t) signal:
//   ./project0 --play

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
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
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
static int16_t fg[RATE * 10];
static int16_t bg[RATE / 5];
static snd_pcm_t *playback;
static int volume = 8192;

static void *play_bg(void *args) {
  E(snd_pcm_prepare, playback);
  const int half = ARRAY_SIZE(fg) / ARRAY_SIZE(bg) / 2;
  for (int i = -half; i < half; i++) {
    for (int j = 0; j < ARRAY_SIZE(bg); j++) {
      bg[j] = cos(j * (abs(i) + 10) * M_PI / (RATE / 100.)) * volume;
    }
    E(snd_pcm_writei, playback, bg, ARRAY_SIZE(bg));
  }
  E(snd_pcm_drain, playback);
  return NULL;
}

int main(int argc, char **argv) {
  bool play = false;
  bool record = false;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--play") == 0) {
      play = true;
    } else if (strcmp(argv[i], "--record") == 0) {
      record = true;
    } else if (strncmp(argv[i], S_LEN("--volume=")) == 0) {
      volume = atoi(argv[i] + LEN("--volume="));
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }

  if (!play && !record) {
    fprintf(stderr, "Not enough arguments\n");
    return EXIT_FAILURE;
  }

  E(snd_pcm_open, &playback, device, SND_PCM_STREAM_PLAYBACK, 0);
  E(snd_pcm_set_params, playback, SND_PCM_FORMAT_S16_LE,
    SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 15000);

  pthread_t bg_thread;
  if (play && record) {
    pthread_create(&bg_thread, NULL, play_bg, NULL);
  }

  if (record) {
    snd_pcm_t *capture;
    E(snd_pcm_open, &capture, device, SND_PCM_STREAM_CAPTURE, 0);
    E(snd_pcm_set_params, capture, SND_PCM_FORMAT_S16_LE,
      SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1, 15000);
    E(snd_pcm_prepare, capture);
    E(snd_pcm_readi, capture, fg, ARRAY_SIZE(fg));
    E(snd_pcm_drain, capture);
    E(snd_pcm_close, capture);
  } else {
    for (int i = 0; i < ARRAY_SIZE(fg); i++) {
      double t = i / (double)RATE;
      fg[i] = (sin(2 * M_PI * 1000 * t) + sin(2 * M_PI * 10000 * t)) * volume;
    }
  }

  if (play && record) {
    pthread_join(bg_thread, NULL);
  }

  E(snd_pcm_prepare, playback);
  E(snd_pcm_writei, playback, fg, ARRAY_SIZE(fg));
  E(snd_pcm_drain, playback);
  E(snd_pcm_close, playback);
  return EXIT_SUCCESS;
}
