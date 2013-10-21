CFLAGS ?= -Os -Wall
LDFLAGS ?= -s

all: cbin

cbin: cbin.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	
clean:
	rm -f cbin

.PHONY: all clean
