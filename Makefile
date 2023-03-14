CC = gcc
CFLAGS = -Wall $(shell pkg-config --cflags libusb libusb-1.0 cfitsio) \
	 -Warray-bounds
LDFLAGS = $(shell pkg-config --libs libusb libusb-1.0 cfitsio) \
	  -lpthread -lncurses

SRCS = thermapp.c main.c
DEPS = thermapp.h

EXEC = astrotherm

OBJS = $(SRCS:.c=.o)

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $<

$(EXEC): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

all: $(EXEC)


.PHONY: clean
clean:
	rm -f $(OBJS)
