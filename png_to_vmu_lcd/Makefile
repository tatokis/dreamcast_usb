CC=gcc
LD=$(CC)
CFLAGS=-Wall -g $(shell libpng-config --cflags)
LDFLAGS=$(shell libpng-config --libs)

PROG=png_to_vmu_lcd
OBJS=main.o

all: $(PROG)

clean:
	rm $(PROG) $(OBJS)

$(PROG): $(OBJS)
	$(LD) $^ -o $@ $(LDFLAGS)


