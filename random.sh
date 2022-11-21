#!/bin/bash
for i in {1..10}; do tr -dc 'A-Za-z0-9!"#$%&'\''()*+,-./:;<=>?@[\]^_`{|}~' < /dev/urandom | head -c 20; echo; sleep 1; done
