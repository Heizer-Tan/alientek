TARGET = webserver
SRCS = webserver.c webserver-http.c webserver-samples.c webserver-icm.c webserver-upgrade.c
CFLAGS ?= -Wall -O2
LDFLAGS ?=
LDLIBS = -lsqlite3

all: $(TARGET)
$(TARGET): $(SRCS) webserver.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f $(TARGET)
