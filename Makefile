all: athernet udp_test

athernet: *.c *.h
	$(CC) athernet.c physical.c backend_alsa.c -lasound -lm -pthread -oathernet

udp_test: udp_test.c
	$(CC) udp_test.c -oudp_test
