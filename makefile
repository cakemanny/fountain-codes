CC=gcc -std=c11
CFLAGS=-g -Wall -c -O0 -fms-extensions
LD=gcc
LFLAGS=-flto
LLIBS=-lws2_32
TARGETS=fountain.exe server.exe

all: $(TARGETS)

fountain.exe: main.o fountain.o errors.o
	$(LD) $(LFLAGS) -o $@ $^ $(LLIBS)

server.exe: server.o errors.o
	$(LD) $(LFLAGS) -o $@ $^ $(LLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $<

clean:
	rm -f *.o $(TARGETS)

