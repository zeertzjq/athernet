all: alsa

alsa:
	$(CC) transmit.c physical.c -lasound -lm -pthread -otransmit backend_alsa.c
	$(CC) receive.c physical.c -lasound -lm -pthread -oreceive backend_alsa.c

debug:
	$(CC) transmit.c physical.c -lm -pthread -otransmit_debug backend_debug.c
	$(CC) receive.c physical.c -lm -pthread -oreceive_debug backend_debug.c
