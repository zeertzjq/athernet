all: athernet athernet_noack

athernet: *.c *.h
	$(CC) athernet.c physical.c backend_alsa.c -lasound -lm -pthread -oathernet

athernet_noack: *.c *.h
	$(CC) athernet_noack.c physical.c backend_alsa.c -lasound -lm -pthread -oathernet_noack
