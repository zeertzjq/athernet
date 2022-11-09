all: athernet

athernet:
	$(CC) athernet.c physical.c backend_alsa.c -lasound -lm -pthread -oathernet
