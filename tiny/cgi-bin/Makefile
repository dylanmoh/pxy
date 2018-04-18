CC = gcc
CFLAGS = -O2 -Wall -I ..

all: adder slow

adder: adder.c
	$(CC) $(CFLAGS) -o adder adder.c

slow: slow.c
	$(CC) $(CFLAGS) -o slow slow.c

clean:
	rm -f adder slow *~
