#ifndef ATNET_COMMON_H
#define ATNET_COMMON_H

#include <math.h>

#define RATE 48000
#define BIT_LEN 48
#define FRAME_BITS 80
#define PREAMBLE_LEN 480
#define HALF_PREAMBLE_LEN 240
#define LEN(s) (sizeof(s) - 1)
#define S_LEN(s) (s), LEN(s)

#define GET_CARRIER(buf)                                                       \
  do {                                                                         \
    for (int i = 0; i < RATE; i++) {                                           \
      double t = i / (double)RATE;                                             \
      buf[i] = sin(2 * M_PI * 10000 * t);                                      \
    }                                                                          \
  } while (0)

#define GET_PREAMBLE(buf, volume)                                              \
  for (int i = 0; i < HALF_PREAMBLE_LEN; i++) {                                \
    double t = i / 24. + i * i / 2880.;                                        \
    buf[PREAMBLE_LEN - 1 - i] = buf[i] = cos(2 * M_PI * t) * volume;           \
  }

#endif // ATNET_COMMON_H
