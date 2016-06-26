SHELL = /bin/sh
.SUFFIXES:

PLATFORM=$(shell uname)
ARCH=$(shell uname -m)

# Basically we want to use gcc with c11 semantics
ifndef CC
  CC=gcc -std=gnu11
else ifeq "$(CC)" "cc"
  CC=gcc -std=gnu11
#else ifeq (,$(findstring gcc,$(CC)))
#  CC=gcc -std=gnu11
else ifeq (,$(findstring std,$(CC)))
  CC += -std=gnu11
endif

# We will rarely want a release build so only when release is defined
ifdef RELEASE
  CFLAGS=-DNDEBUG -Wall -g -c -O3 -fms-extensions -march=native -fno-omit-frame-pointer
  LDFLAGS=

  ifeq "$(PLATFORM)" "Darwin"
    #want to include -flto if using clang rather than gcc TODO lookup make &&
    ifeq "clang" "$(findstring clang,$(CC))"
      CC+= -Wno-microsoft # TODO look at removing use of the anon union
    else
      CFLAGS+=-Wa,-q # clang gives warning using this argument
      LDFLAGS+=-fwhole-program
    endif
  else ifeq "$(OS)" "Windows_NT"
    # Do nothing - link-time-opt seems to break the program on windwos
  else
	# using lto seems to make us slower on linux
    #CC+=-flto -fuse-linker-plugin #lto-wrapper ignores our -Wa,-q
  endif # Platform

else # Not RELEASE
  CFLAGS=-g -Wall -c -O0 -fms-extension
  LDFLAGS=
endif # End RELEASE-IF

ifdef PROFILE
  CFLAGS=-pg -Wall -c -O0 -fms-extensions -march=native
  LDFLAGS=-pg
endif

ifdef SANIT
  CC=clang -std=gnu11
  CFLAGS+=-fsanitize=$(SANIT) -g -Wno-microsoft -fno-omit-frame-pointer -O0
  LDFLAGS+=-fsanitize=$(SANIT)
endif

# Use the doxygen checking feature if using clang
ifeq "clang" "$(findstring clang,$(CC))"
  CFLAGS+=-Wdocumentation
endif

LDLIBS=-lm
ifeq "$(OS)" "Windows_NT"
  LDLIBS+=-lws2_32
endif
ifeq "$(PLATFORM)" "SunOS"
  LDLIBS+=-lsocket -lnsl
endif

# Define a function for adding .exe
ifeq "$(OS)" "Windows_NT"
  wino = $(1).exe
else
  wino = $(1)
endif

TARGETS := fountain server client
TARGETS := $(foreach target,$(TARGETS),$(call wino,$(target)))

TEST_TARGETS := fountain_test
TEST_TARGETS := $(foreach target,$(TEST_TARGETS),$(call wino,$(target)))

all: $(TARGETS)

tests: $(TEST_TARGETS)

tests: CFLAGS+= -DUNIT_TESTS

test: tests
	./fountain_test

$(call wino,fountain): main.o fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(call wino,server): server.o fountain.o errors.o mapping.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(call wino,client): client.o fountain.o errors.o mapping.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(call wino,fountain_test): fountain.o errors.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $<

.PHONY: clean

clean:
	rm -f *.o $(TARGETS) $(TEST_TARGETS)

