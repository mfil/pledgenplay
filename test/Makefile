CC=gcc
CFLAGS=-g -std=$(STD) -pedantic -Wall $(IDIRS)
STD=c99
IDIRS=-I/usr/local/include -I..
LDIRS=-L/usr/local/lib
LIBS=-lcheck -lutil -lsndio -liconv -lFLAC
TESTS=test_child_main test_child_messages test_decoder test_decode_to_file \
    test_input_file test_output_file

all: $(TESTS)

run-all: $(TESTS)
	for test in $(TESTS); do ./$${test}; done

child_main.o input_file.o flac.o out_sndio.o parent_main.o child_errors.o \
    child_messages.o decoder.o id3v2.o output_file.o pnp_child:
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

test_input_file: test_input_file.c id3v2.o input_file.o mock_errors.o
	$(CC) $(CFLAGS) $(LDIRS) $(LIBS) -o test_input_file ../obj/id3v2.o \
	    ../obj/input_file.o mock_errors.o test_input_file.c

test_decoder: test_decoder.c input_file.o id3v2.o decoder.o mock_errors.o
	$(CC) $(CFLAGS) $(LDIRS) $(LIBS) -o test_decoder ../obj/decoder.o \
	    ../obj/input_file.o ../obj/id3v2.o mock_errors.o test_decoder.c

test_output_file: output_file.o mock_errors.o test_output_file.c
	$(CC) $(CFLAGS) $(LDIRS) $(LIBS) -o test_output_file mock_errors.o \
	    ../obj/output_file.o test_output_file.c

test_decode_to_file: output_file.o decoder.o mock_errors.o test_decode_to_file.c
	$(CC) $(CFLAGS) $(LDIRS) $(LIBS) -o test_decode_to_file mock_errors.o \
	    ../obj/input_file.o ../obj/output_file.o ../obj/decoder.o \
	    ../obj/id3v2.o test_decode_to_file.c

test_child_main: pnp_child test_child_main.c
	$(CC) $(CFLAGS) $(LDIRS) $(LIBS) -o test_child_main test_child_main.c
