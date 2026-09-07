CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

ota-agent: ota-agent.c ota-agent.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ ota-agent.c

.PHONY: clean

clean:
	rm -f ota-agent
