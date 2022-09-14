// Record 10 seconds of sound (with optional background sound) and play it.
//
// To compile:
//   gcc project0.c -lasound -lm -oproject0
//
// To run with background sound, don't pass any arguments:
//   ./project0
//
// To run without background sound, pass any arguments:
//   ./project0 .

#include <alsa/asoundlib.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RATE 48000
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static const char *device = "default";
static int16_t fg_buf[RATE * 10];
static int16_t bg_buf[RATE / 5];
static snd_pcm_t *capture;
static snd_pcm_t *playback;

static void *play_bg(void *args) {
  const int half = ARRAY_SIZE(fg_buf) / ARRAY_SIZE(bg_buf) / 2;
  for (int i = -half; i < half; i++) {
    for (int j = 0; j < ARRAY_SIZE(bg_buf); j++) {
      bg_buf[j] = cos(j * (abs(i) + 10) * M_PI / (RATE / 100.)) * 2000.;
    }
    snd_pcm_writei(playback, bg_buf, ARRAY_SIZE(bg_buf));
  }
  return NULL;
}

int main(int argc, char **argv) {
  int err;

  if ((err = snd_pcm_open(&capture, device, SND_PCM_STREAM_CAPTURE, 0)) < 0) {
    fprintf(stderr, "Capture open error: %s\n", snd_strerror(err));
    return EXIT_FAILURE;
  }

  if ((err = snd_pcm_set_params(capture, SND_PCM_FORMAT_S16_LE,
                                SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1,
                                15000)) < 0) {
    fprintf(stderr, "Capture open error: %s\n", snd_strerror(err));
    return EXIT_FAILURE;
  }

  if ((err = snd_pcm_open(&playback, device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
    fprintf(stderr, "Playback open error: %s\n", snd_strerror(err));
    return EXIT_FAILURE;
  }

  if ((err = snd_pcm_set_params(playback, SND_PCM_FORMAT_S16_LE,
                                SND_PCM_ACCESS_RW_INTERLEAVED, 1, RATE, 1,
                                15000)) < 0) {
    fprintf(stderr, "Playback open error: %s\n", snd_strerror(err));
    return EXIT_FAILURE;
  }

  pthread_t bg_thread;
  if (argc == 1) {
    pthread_create(&bg_thread, NULL, play_bg, NULL);
  }

  snd_pcm_readi(capture, fg_buf, ARRAY_SIZE(fg_buf));
  snd_pcm_drain(capture);
  snd_pcm_close(capture);

  if (argc == 1) {
    pthread_join(bg_thread, NULL);
  }

  snd_pcm_writei(playback, fg_buf, ARRAY_SIZE(fg_buf));
  snd_pcm_drain(playback);
  snd_pcm_close(playback);
  return EXIT_SUCCESS;
}
