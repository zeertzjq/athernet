all: athernet

athernet: *.c *.h
	$(CC) athernet.c physical.c backend_alsa.c -lasound -lm -pthread -oathernet
