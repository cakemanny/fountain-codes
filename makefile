CC=gcc
CFLAGS=-c -s -O2 -march=native
LD=gcc
LFLAGS=-s -flto
LLIBS=-lws2_32
TARGETS=fountain.exe server.exe
OBJ=main.o fountain.o

all: $(TARGETS)

%.exe: $(OBJ)
	$(LD) $(LFLAGS) -o $@ $^ $(LLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $<

clean:
	rm -f *.o $(TARGETS)

