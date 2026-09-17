# key-monitor：Yocto 与宿主机均可 make / make install
CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS ?=
PREFIX ?= /usr
DESTDIR ?=
# 宿主机：相对 recipes-apps/common；Yocto do_configure 会把源拷到本目录
COMMON_SRC ?= ../../../common
ifneq ($(wildcard input-device.c),)
INPUT_SRC := input-device.c
else
INPUT_SRC := $(COMMON_SRC)/input-device.c
CFLAGS += -I$(COMMON_SRC)
endif

.PHONY: all clean install

all: key-monitor

key-monitor: key-monitor.c $(INPUT_SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ key-monitor.c $(INPUT_SRC)

install: key-monitor
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 key-monitor $(DESTDIR)$(PREFIX)/bin/key-monitor

clean:
	rm -f key-monitor
