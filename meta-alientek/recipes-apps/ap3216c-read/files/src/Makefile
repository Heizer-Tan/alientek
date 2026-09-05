CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2
PREFIX ?= /usr
DESTDIR ?=

.PHONY: all clean install

all: ap3216c-read

ap3216c-read: ap3216c-read.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

install: ap3216c-read
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 ap3216c-read $(DESTDIR)$(PREFIX)/bin/ap3216c-read

clean:
	rm -f ap3216c-read
