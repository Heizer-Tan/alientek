# touch-monitor：Yocto 与宿主机均可 make / make install
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS ?=
PREFIX ?= /usr
DESTDIR ?=
COMMON_SRC ?= ../../../common
ifneq ($(wildcard input-device.c),)
INPUT_SRC := input-device.c
else
INPUT_SRC := $(COMMON_SRC)/input-device.c
CFLAGS += -I$(COMMON_SRC)
endif

.PHONY: all clean install

all: touch-monitor

touch-monitor: touch-monitor.c $(INPUT_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ touch-monitor.c $(INPUT_SRC)

install: touch-monitor
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 touch-monitor $(DESTDIR)$(PREFIX)/bin/touch-monitor

clean:
	rm -f touch-monitor
