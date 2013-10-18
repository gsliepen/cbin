CFLAGS ?= -Os -Wall
LDFLAGS ?= -s

all: cbin

cbin: cbin.c
	c99 $(CFLAGS) $(LDFLAGS) -o $@ $<
	
clean:
	rm -f cbin

.PHONY: all clean
