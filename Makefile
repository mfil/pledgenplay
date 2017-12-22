.SUFFIXES: .c .o

CC=gcc
CFLAGS=-g -std=$(STD) -pedantic -Wall $(IDIRS) $(LDIRS)
STD=c99
IDIRS=-I/usr/local/include
LDIRS=-L/usr/local/lib
LIBS=-lutil -lsndio -liconv -lFLAC
TESTDIR=test
DEPENDS=pnp.h comm.h child.h flac.h out_sndio.h child_messages.h \
    child_errors.h message_types.h
CHILD_OBJS= child_main.o child_errors.o child_messages.o decoder.o \
    input_file.o output_file.o id3v2.o

pnp: main.o child_main.o child_messages.o child_errors.o file.o flac.o out_sndio.o parent_main.o
	$(CC) $(CFLAGS) $(IDIRS) $(LDIRS) $(LIBS) -o pnp main.o child_main.o \
	    child_messages.o child_errors.o file.o flac.o out_sndio.o parent_main.o

pnp_child: $(CHILD_OBJS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ $(CHILD_OBJS)

test: decode_test ipc_test

clean:
	rm -f $(.OBJDIR)/*

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<
*.o:	$(DEPENDS)
