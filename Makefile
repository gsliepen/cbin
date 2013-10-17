CFLAGS ?= -Os -flto -Wall
LDFLAGS ?= -s

all: cbin

%.o: %.c
	c99 $(CFLAGS) -c -o $@ $<

cbin: cbin.o
	c99 $(CFLAGS) $(LDFLAGS) -o $@ $<
	
clean:
	rm -f *.o cbin

.PHONY: all clean
