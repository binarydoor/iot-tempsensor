
CC = gcc
CFLAGS = -lm -lmraa -g -Wall -Wextra -lssl -lcrypto
DISTFILES = iottemp_tcp.c iottemp_tls.c Makefile README

default: build


build: iottemp_tcp iottemp_tls


iottemp_tcp: iottemp_tcp.c
	$(CC) $(CFLAGS) iottemp_tcp.c -o iottemp_tcp


iottemp_tls: iottemp_tls.c
	$(CC) $(CFLAGS) iottemp_tls.c -o iottemp_tls

clean:
	rm -rf *.tar.gz iottemp_tcp iottemp_tls
