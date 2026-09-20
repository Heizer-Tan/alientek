CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
LDLIBS = -lsqlite3
PREFIX ?= /usr
DESTDIR ?=

.PHONY: all clean install

all: icm20608-logger

icm20608-logger: icm20608-logger.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

install: icm20608-logger
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 icm20608-logger $(DESTDIR)$(PREFIX)/bin/icm20608-logger

clean:
	rm -f icm20608-logger
