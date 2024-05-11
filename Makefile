CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=gnu99
LIBS = -lcurl

pushd: pushd.c
	$(CC) $(CFLAGS) -o pushd pushd.c $(LIBS)

.PHONY: clean
clean:
	rm -f pushd

install: pushd
	cp -f pushd /usr/local/bin
	@if [ ! -f /etc/pushd.conf ]; then set -x; cp pushd.conf.example /etc/pushd.conf; fi
