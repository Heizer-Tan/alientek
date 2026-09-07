CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

SOURCES = ota-agent.c ota-state.c ota-download.c ota-exec.c ota-mqtt.c
HEADERS = ota-agent.h ota-state.h ota-download.h ota-exec.h ota-mqtt.h

ota-agent: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES)

.PHONY: clean

clean:
	rm -f ota-agent
