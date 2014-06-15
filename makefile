SHELL = /bin/sh
.SUFFIXES:

# Basically we want to use gcc with c11 semantics
ifndef CC
CC=gcc -std=gnu11
else ifeq (,$(findstring gcc,$(CC)))
CC=gcc -std=gnu11
else ifeq (,$(findstring std,$(CC)))
CC += -std=gnu11
endif

# We will rarely want a release build so only when release is defined
ifdef RELEASE
#CFLAGS=-DNDEBUG -Wall -c -O3 -fms-extensions -march=native -Wa,-q -mavx2
CFLAGS=-DNDEBUG -Wall -c -O3 -fms-extensions -march=corei7 -Wa,-q -mavx2
LDFLAGS=-flto -lm
else
CFLAGS=-g -Wall -c -O0 -fms-extensions
LDFLAGS=-lm
endif

ifdef PROFILE
CFLAGS=-pg -Wall -c -O0 -fms-extensions
LDFLAGS=-pg -lm
endif

LDLIBS=
ifeq "$(OS)" "Windows_NT"
LDLIBS=-lws2_32
endif

# Define a function for adding .exe
ifeq "$(OS)" "Windows_NT"
wino = $(1).exe
else
wino = $(1)
endif

TARGETS := fountain server client
TARGETS := $(foreach target,$(TARGETS),$(call wino,$(target)))

all: $(TARGETS)

$(call wino,fountain): main.o fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(call wino,server): server.o fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(call wino,client): client.o fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(LDFLAGS) $<

.PHONY: clean

clean:
	rm -f *.o $(TARGETS)

