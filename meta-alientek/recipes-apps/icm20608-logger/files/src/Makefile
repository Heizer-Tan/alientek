CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDLIBS = -lsqlite3 -lm
PREFIX ?= /usr
DESTDIR ?=

.PHONY: all clean install

all: icm20608-logger

icm20608-logger: icm20608-logger.c iio-icm.c iio-icm.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ icm20608-logger.c iio-icm.c $(LDLIBS)

install: icm20608-logger
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 icm20608-logger $(DESTDIR)$(PREFIX)/bin/icm20608-logger

clean:
	rm -f icm20608-logger
