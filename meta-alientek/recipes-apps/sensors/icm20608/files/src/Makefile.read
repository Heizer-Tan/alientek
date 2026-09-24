CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
PREFIX ?= /usr
DESTDIR ?=

.PHONY: all clean install

all: icm20608-read

icm20608-read: icm20608-read.c iio-icm.c iio-icm.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ icm20608-read.c iio-icm.c -lm

install: icm20608-read
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 icm20608-read $(DESTDIR)$(PREFIX)/bin/icm20608-read

clean:
	rm -f icm20608-read
