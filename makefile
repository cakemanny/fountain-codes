CC=gcc
CFLAGS=-c -s -O2 -march=native
LD=gcc
LFLAGS=-s -flto
LLIBS=-lws2_32
TARGETS=fountain.exe server.exe
FOUNT_OBJ=main.o fountain.o

all: $(TARGETS)

fountain.exe: $(FOUNT_OBJ)
	$(LD) $(LFLAGS) -o $@ $^ $(LLIBS)

server.exe: server.c
	$(LD) $(LFLAGS) -o $@ $^ $(LLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $<

clean:
	rm -f *.o $(TARGETS)

