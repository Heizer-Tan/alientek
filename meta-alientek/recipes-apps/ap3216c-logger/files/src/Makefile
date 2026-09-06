TARGET = ap3216c-logger
SRCS = ap3216c-logger.c
CFLAGS ?= -Wall -O2
LDFLAGS ?=
LDLIBS = -lsqlite3

all: $(TARGET)
$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)

clean:
	rm -f $(TARGET)
