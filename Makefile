.SUFFIXES: .c .o

CC=gcc
CFLAGS=-g -std=$(STD) -pedantic -Wall $(IDIRS) $(LDIRS)
STD=c99
IDIRS=-I/usr/local/include
LDIRS=-L/usr/local/lib
LIBS=-lcheck -lutil
TESTDIR=test
DEPENDS=pnp.h comm.h flac.h

all: child_main.o file.o flac.o parent_main.o

test: decode_test ipc_test

clean:
	rm -f $(.OBJDIR)/*

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<
*.o:	$(DEPENDS)
