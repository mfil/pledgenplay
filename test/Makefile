CC=gcc
CFLAGS=-g -std=$(STD) -pedantic -Wall $(IDIRS)
STD=c99
IDIRS=-I/usr/local/include -I..
LDIRS=-L/usr/local/lib
LIBS=-lcheck -lutil -lsndio -liconv -lFLAC

all: test_child_messages decode_test ipc_test

child_main.o input_file.o flac.o out_sndio.o parent_main.o child_errors.o \
    child_messages.o:
	cd ..; make $@

mock_errors.o: mock_errors.c mock_errors.h
	$(CC) $(CFLAGS) -o $@ -c mock_errors.c

clean:
	rm ./decode_test ./ipc_test ./test_child_messages

decode_test: decode_test.c child_main.o child_messages.o child_errors.o \
    input_file.o flac.o out_sndio.o parent_main.o
	$(CC) $(CFLAGS)  $(LDIRS) $(LIBS) -o decode_test ../obj/child_main.o \
	    ../obj/child_messages.o ../obj/child_errors.o ../obj/flac.o \
	    ../obj/input_file.o ../obj/out_sndio.o ../obj/parent_main.o \
	    decode_test.c

ipc_test: ipc_test.c child_main.o child_messages.o child_errors.o out_sndio.o \
    parent_main.o
	$(CC) $(CFLAGS)  $(LDIRS) $(LIBS) -o ipc_test ../obj/child_main.o \
	    ../obj/child_messages.o ../obj/child_errors.o ../obj/file.o \
	    ../obj/flac.o ../obj/out_sndio.o ../obj/parent_main.o ipc_test.c

test_child_messages: test_child_messages.c child_messages.o mock_errors.o
	$(CC) $(CFLAGS)  $(LDIRS) $(LIBS) -o test_child_messages \
        ../obj/child_messages.o mock_errors.o test_child_messages.c

test_input_file: test_input_file.c input_file.o mock_errors.o
	$(CC) $(CFLAGS) $(LDIRS) $(LIBS) -o test_input_file \
	    ../obj/input_file.o mock_errors.o test_input_file.c
