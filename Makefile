all: alsa

alsa:
	$(CC) transmit.c physical.c -lasound -lm -pthread -otransmit backend_alsa.c
	$(CC) receive.c physical.c -lasound -lm -pthread -oreceive backend_alsa.c
