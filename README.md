# ATNet

gcc receive.c physical.c backend_alsa.c -o recieve -lm -lpthread -lasound

gcc transmit.c physical.c backend_alsa.c -o transmit -lm -lpthread -lasound
