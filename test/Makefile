CC=gcc
CFLAGS=-g -std=$(STD) -pedantic -Wall $(IDIRS) $(LDIRS) $(LIBS)
STD=c99
IDIRS=-I/usr/local/include -I..
LDIRS=-L/usr/local/lib
LIBS=-lcheck -lutil -lsndio -liconv -lFLAC

all: decode_test ipc_test
child_main.o file.o flac.o out_sndio.o parent_main.o:
	cd ..; make $@

clean:
	rm ./decode_test ./ipc_test

decode_test: decode_test.c child_main.o file.o flac.o out_sndio.o parent_main.o
	$(CC) $(CFLAGS) -o decode_test ../obj/child_main.o ../obj/flac.o \
	    ../obj/file.o ../obj/out_sndio.o ../obj/parent_main.o decode_test.c

ipc_test: ipc_test.c child_main.o out_sndio.o parent_main.o
	$(CC) $(CFLAGS) -o ipc_test ../obj/child_main.o ../obj/file.o \
	    ../obj/flac.o ../obj/out_sndio.o ../obj/parent_main.o ipc_test.c
