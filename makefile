ifndef CC
	CC=gcc -std=c11
endif
CFLAGS=-g -Wall -c -O0 -fms-extensions
#CFLAGS= -Wall -c -O3 -fms-extensions -march=native
LDFLAGS=
#LDFLAGS=-flto

ifeq "$(OS)" "Windows_NT" 
	LDLIBS=-lws2_32
	TARGETS=fountain.exe server.exe
else
	TARGETS=fountain server
endif


all: $(TARGETS)

fountain: main.o fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

server: server.o fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) $<

clean:
	rm -f *.o $(TARGETS)

