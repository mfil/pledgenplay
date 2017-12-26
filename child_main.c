/*
 * Copyright (c) 2016 Max Fillinger
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/limits.h>
#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include "child_errors.h"
#include "child_messages.h"
#include "decoder.h"
#include "input_file.h"
#include "message_types.h"
#include "output.h"

static int start_play(const struct output *);

int
main(int argc, char **argv)
{
	if (pledge("stdio recvfd audio", NULL) == -1)
		return (-1);

	/* The socket for communicating with the parent should have
	 * been passed in argv. */

	if (argc < 2) {
		ipc_error("No socket for communicating with the parent.");
	}
	const char *errstr;
	int socket = (int)strtonum(argv[1], 0, INT_MAX, &errstr);
	if (errstr != NULL) {
		ipc_error("Invalid file descriptor.");
	}

	/* Check if the received file descriptor is indeed a socket. */

	struct stat sb;
	if (fstat(socket, &sb) != 0) {
		ipc_error("fstat");
	}
	if (! (S_ISSOCK(sb.st_mode))) {
		ipc_error("received file descriptor is not a socket.");
	}

	initialize_ipc(socket);

	/* Send hello message to parent. */

	enqueue_message(MSG_HELLO, NULL);
	while (send_messages() == SEND_MSG_SOCKET_NOT_READY)
		;

	const struct output *out = NULL;
	int playing = 0;
	int decoder_finished = 0;
	struct decoded_frame *frame = NULL;
	while (1) {

		/* Process messages from parent. */

		check_for_messages();
		struct message message;
		const struct audio_parameters *params = NULL;
		while (get_next_message(&message) == GOT_MESSAGE) {
			switch (message.type) {
			case (CMD_EXIT):
				_exit(0);
				break;
			case (CMD_SET_INPUT):
				set_new_input_file(message.data.fd);
				decoder_initialize();
				decoder_finished = 0;
				break;
			case (CMD_SET_OUTPUT_FILE_RAW):
				out = output_raw(message.data.fd);
				break;
			case (CMD_SET_OUTPUT_FILE_WAV):
				out = output_wav(message.data.fd);
				break;
			case (CMD_SET_OUTPUT_SNDIO):
				break;
			case (CMD_META):
				break;
			case (CMD_PLAY):
				playing = start_play(out);
				break;
			case (CMD_PAUSE):
				break;
			default:
				child_fatalx("Received invalid message.");
				break;
			}
		}

		/* Send messages to parent. */

		send_messages();

		if (playing) {
			if (! decoder_finished && out->ready_for_new_frame()) {
				free_decoded_frame(frame);
				DECODER_DECODE_STATUS status;
				status = decoder_decode_next_frame(&frame);
				if (status == DECODER_DECODE_FINISHED) {
					decoder_finished = 1;
				}
				else if (status == DECODER_DECODE_ERROR) {
					child_fatalx("decoding error");
				}
				else {
					out->next_frame(frame);
				}
			}

			OUTPUT_RUN_STATUS status;
			status = out->run();
			if (status == OUTPUT_ERROR) {
				child_fatalx("output error");
			}

			if (decoder_finished && status == OUTPUT_IDLE) {
				playing = 0;
				out->close();
				out = NULL;
				enqueue_message(MSG_DONE, NULL);
			}
		}
	}
}

static int
start_play(const struct output *out)
{
	/* Check if the input and output are open. */

	int got_input = 0;
	int got_output = 0;
	if (input_file_is_open()) {
		got_input = 1;
	}
	else {
		input_errx("No input.");
	}
	
	if (out != NULL) {
		got_output = 1;
	}
	else {
		output_errx("No output.");
	}

	if (! got_input || ! got_output) {
		return (0);
	}

	/* Set output parameters. */

	const struct audio_parameters *params = decoder_get_parameters();
	if (params == NULL) {
		child_fatalx("Couldn't get parameters");
	}
	if (out->set_parameters(params) == OUTPUT_PARAMETERS_ERROR) {
		child_fatalx("Couldn't set parameters");
	}

	return (1);
}
