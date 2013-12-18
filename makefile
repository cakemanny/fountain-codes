SHELL = /bin/sh
.SUFFIXES:

# Basically we want to use gcc with c11 semantics
ifndef CC
CC=gcc -std=c11
else ifeq (,$(findstring gcc,$(CC)))
CC=gcc -std=c11
else ifeq (,$(findstring std,$(CC)))
CC += -std=c11
endif

# We will rarely want a release build so only when release is defined
ifdef RELEASE
CFLAGS= -Wall -c -O3 -fms-extensions -march=native
LDFLAGS=-flto
else
CFLAGS=-g -Wall -c -O0 -fms-extensions
LDFLAGS=
endif

ifeq "$(OS)" "Windows_NT" 
LDLIBS=-lws2_32
TARGETS=fountain.exe \
		server.exe
else
LDLIBS=
TARGETS=fountain \
		server
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

