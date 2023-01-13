all: athernet

athernet: *.c *.h
	$(CC) athernet.c physical.c backend_alsa.c -lasound -lm -pthread -oathernet

ftp_test: *.c *.h
	$(CC) ftp_test.c ftp.c tcp_ip.c -oftp_test
