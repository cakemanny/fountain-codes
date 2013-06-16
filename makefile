CC=gcc -std=c11
CFLAGS=-g -Wall -c -O0 -fms-extensions
LDFLAGS=
LDLIBS=-lws2_32
TARGETS=fountain.exe server.exe

all: $(TARGETS)

fountain.exe: main.o fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

server.exe: server.o fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) $<

clean:
	rm -f *.o $(TARGETS)

