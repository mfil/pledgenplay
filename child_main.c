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

	/* Check if the received file descriptor is indeed a socket */

	struct stat sb;
	if (fstat(socket, &sb) != 0) {
		ipc_error("fstat");
	}
	if (! (S_ISSOCK(sb.st_mode))) {
		ipc_error("received file descriptor is not a socket.");
	}

	initialize_ipc(socket);

	while (1) {

		/* Process messages from parent. */

		check_for_messages();
		struct message message;
		while (get_next_message(&message) == GOT_MESSAGE) {
			switch (message.type) {
			case (CMD_NEW_INPUT_FILE):
				set_new_input_file(message.data.fd);
				decoder_initialize();
			case (CMD_EXIT):
				_exit(0);
				break;
			case (CMD_META):
			case (CMD_PLAY):
			case (CMD_PAUSE):
			default:
				child_fatalx("Received invalid message.");
				break;
			}
		}

		/* Send messages to parent. */

		send_messages();
	}
}
