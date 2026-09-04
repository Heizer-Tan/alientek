# key-monitor：Yocto 与宿主机均可 make / make install
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS ?=
PREFIX ?= /usr
DESTDIR ?=

.PHONY: all clean install

all: key-monitor

key-monitor: key-monitor.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

install: key-monitor
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 key-monitor $(DESTDIR)$(PREFIX)/bin/key-monitor

clean:
	rm -f key-monitor
