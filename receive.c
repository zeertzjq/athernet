#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "common.h"
#include "physical.h"

int main(int argc, char **argv) {

  for (int i = 1; i < argc; i++) {
    } else {
      fprintf(stderr, "Invalid argument: %s\n", argv[i]);
      return EXIT_FAILURE;
    }
  }

  int receive_frames = 1 + (receive_bytes * 8 - 1) / FRAME_BITS;

  phy_init();


  return EXIT_SUCCESS;
}
