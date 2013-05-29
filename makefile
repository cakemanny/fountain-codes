CC=gcc -std=c11
CFLAGS=-g -Wall -c -O3
LD=gcc
LFLAGS=-flto
LLIBS=-lws2_32
TARGETS=fountain.exe server.exe
FOUNT_OBJ=main.o fountain.o

all: $(TARGETS)

fountain.exe: $(FOUNT_OBJ)
	$(LD) $(LFLAGS) -o $@ $^ $(LLIBS)

server.exe: server.o
	$(LD) $(LFLAGS) -o $@ $^ $(LLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $<

clean:
	rm -f *.o $(TARGETS)

