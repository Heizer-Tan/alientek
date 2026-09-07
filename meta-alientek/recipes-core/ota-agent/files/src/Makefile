CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

ota-agent: ota-agent.c ota-agent.h ota-state.c ota-state.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ ota-agent.c ota-state.c

.PHONY: clean

clean:
	rm -f ota-agent
