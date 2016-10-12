.SUFFIXES: .c .o

CC=gcc
CFLAGS=-g -std=$(STD) -pedantic -Wall $(IDIRS) $(LDIRS)
STD=c99
IDIRS=-I/usr/local/include
LDIRS=-L/usr/local/lib
LIBS=-lutil -lsndio -liconv -lncurses -lFLAC
TESTDIR=test
DEPENDS=pnp.h comm.h child.h flac.h out_sndio.h

pnp: main.o child_main.o file.o flac.o out_sndio.o parent_main.o
	$(CC) $(CFLAGS) $(IDIRS) $(LDIRS) $(LIBS) -o pnp main.o child_main.o \
	    file.o flac.o out_sndio.o parent_main.o

test: decode_test ipc_test

clean:
	rm -f $(.OBJDIR)/*

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<
*.o:	$(DEPENDS)
