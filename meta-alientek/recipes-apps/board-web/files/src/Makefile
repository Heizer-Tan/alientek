TARGET = board-web
SRCS = board-web.c
CFLAGS ?= -Wall -O2
LDFLAGS ?=
LDLIBS = -lsqlite3

all: $(TARGET)
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f $(TARGET)
