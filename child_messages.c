/*
 * Copyright (c) 2017 Max Fillinger
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

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <imsg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "child_errors.h"
#include "child_messages.h"
#include "message_types.h"

#define IMSG_FAILURE			(-1)
#define IMSG_GET_NO_MESSAGES		(0)
#define IMSG_MAX_MESSAGE_LENGTH		(UINT16_MAX)

static struct imsgbuf	ibuf;

void
initialize_ipc(int fd)
{
	/*
	 * Initialize the data structures for communicating with the parent
	 * process.
	 */
	imsg_init(&ibuf, fd);
}

void
send_messages(void)
{
	/* Send the enqueued messages to the parent. */
	if (imsg_flush(&ibuf) == IMSG_FAILURE) {
		ipc_error("imsg_flush");
	}
}

void
receive_messages(void)
{
	/*
	 * Receive messages from the parent and make them available for
	 * get_next_message().
	 */
	if (imsg_read(&ibuf) == IMSG_FAILURE) {
		ipc_error("imsg_read");
	}
}

void
enqueue_message(MESSAGE_TYPE type, char *message)
{
	/*
 	 * Enqueue a message to the parent. The message has to be a
	 * null-terminated string and will be truncated if it exceeds the
	 * maximum length for imsg_compose (64 kb, so this should not actually
	 * happen).
 	 */

	/*
	 * Calculate the length of the message, including the terminating
	 * null-byte.
	 */
	size_t message_length = strlen(message) + 1;
	/* Truncate the message, if necessary. */
	if (IMSG_MAX_MESSAGE_LENGTH < message_length) {
		message_length = IMSG_MAX_MESSAGE_LENGTH;
		message[message_length-1] = '\0';
	}
	/*
	 * Enqueue the message. Casting message_length to uint16_t is ok
	 * because it can't be larger after truncation.
	 */
	if (imsg_compose(&ibuf, (uint32_t)type, 0, getpid(), -1, message,
	    (uint16_t)message_length) == IMSG_FAILURE)
		ipc_error("Error in imsg_compose.");
}

GET_NEXT_MESSAGE_STATUS
get_next_message(struct message *message)
{
	/*
	 * Get the next message (if available), return 1 if there is a message
	 * and 0 otherwise.
	 */

	/* Grab the next message from the buffer. Return 0 if there is none. */
	struct imsg	imessage;
	ssize_t		imsg_get_status;
	imsg_get_status = imsg_get(&ibuf, &imessage);
	if (imsg_get_status == IMSG_FAILURE) {
		ipc_error("Error in imsg_get.");
	}
	if (imsg_get_status == IMSG_GET_NO_MESSAGES) {
		return (0);
	}
	/*
	 * Check if the message type is valid and extract additional data if
	 * applicable.
	 */
	switch (imessage.hdr.type) {
	case (CMD_NEW_INPUT_FILE):
		message->type = imessage.hdr.type;
		message->data.fd = imessage.fd;
		break;
	case (CMD_EXIT):
	case (CMD_META):
	case (CMD_PLAY):
	case (CMD_PAUSE):
		message->type = imessage.hdr.type;
		/* Set message->data to a dummy value. */
		message->data.fd = -1;
		break;
	default:
		child_fatalx("Invalid CMD_MESSAGE_TYPE received.");
	}

	imsg_free(&imessage);
	return (1);
}
