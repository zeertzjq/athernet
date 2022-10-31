all: alsa

alsa:
	$(CC) athernet.c physical.c backend_alsa.c -lasound -lm -pthread -oathernet

debug:
	$(CC) athernet.c physical.c backend_debug.c -lasound -lm -pthread -oathernet_debug
