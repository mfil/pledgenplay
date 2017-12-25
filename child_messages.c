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
#include <poll.h>
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

static struct imsgbuf ibuf;
static int fd = -1;

static int is_invalid_message_type(MESSAGE_TYPE);

void
initialize_ipc(int filedesc)
{
	/* Initialize the data structures for communicating with the parent
	 * process. */

	fd = filedesc;
	imsg_init(&ibuf, fd);
}

SEND_MSG_STATUS
send_messages(void)
{
	/* Check if the socket is ready for writing. */

	struct pollfd pfd = {fd, POLLOUT, 0};
	if (poll(&pfd, 1, 0) == -1) {
		ipc_error("poll");
	}
	if (pfd.revents & POLLERR) {
		ipc_error("invalid file descriptor");
	}
	if (pfd.revents & POLLHUP) {
		ipc_error("connection to parent process closed");
	}
	if (!(pfd.revents & POLLOUT)) {
		return (SEND_MSG_SOCKET_NOT_READY);
	}

	/* Send the enqueued messages to the parent. */

	if (imsg_flush(&ibuf) == IMSG_FAILURE) {
		ipc_error("imsg_flush");
	}
	return (SEND_MSG_OK);
}

void
check_for_messages(void)
{
	/* Check if the socket is ready for reading. */

	struct pollfd pfd = {fd, POLLIN, 0};
	if (poll(&pfd, 1, 0) == -1) {
		ipc_error("poll");
	}
	if (pfd.revents & POLLERR) {
		ipc_error("invalid file descriptor");
	}
	if (pfd.revents & POLLHUP) {
		ipc_error("connection to parent closed.");
	}
	if (! (pfd.revents & POLLIN)) {
		return;
	}

	/* Receive messages from the parent and make them available for
	 * get_next_message(). */

	if (imsg_read(&ibuf) == IMSG_FAILURE) {
		ipc_error("imsg_read");
	}
}

void
enqueue_message(MESSAGE_TYPE type, const char *message)
{
	/* Enqueue a message to the parent. The message has to be a
	 * null-terminated string, or NULL if only a message type needs
	 * to be sent. If it exceeds the maximum length for imsg_compose
	 * (64 kb), the message will be ignored and a warning will be
	 * sent to the parent. */

	if (is_invalid_message_type(type)) {
		child_fatalx("Invalid MESSAGE_TYPE in enqueue_message.");
	}

	uint16_t message_length;
	if (message != NULL) {
		size_t message_strlen = strlen(message);

		/* Check if the message length (including the terminating '\0')
		 * exceeds UINT16_MAX, the maximal length for messages in
 		 * imsg_compose(). */

		if (UINT16_MAX - 1 < message_strlen) {
			child_warnx("Attempting to send over-long message.");
			return;
		}

		message_length = (uint16_t)message_strlen;
	}
	else {
		message_length = 0;
	}

	/* Enqueue the message. */

	if (imsg_compose(&ibuf, (uint32_t)type, 0, getpid(), -1, message,
	    message_length) == IMSG_FAILURE)
		ipc_error("Error in imsg_compose.");
}

GET_NEXT_MSG_STATUS
get_next_message(struct message *message)
{
	/* Grab the next message from the buffer and return NO_MESSAGES if
	 * there are none. */

	struct imsg imessage;
	ssize_t imsg_get_status;
	imsg_get_status = imsg_get(&ibuf, &imessage);
	if (imsg_get_status == IMSG_FAILURE) {
		ipc_error("Error in imsg_get.");
	}
	if (imsg_get_status == IMSG_GET_NO_MESSAGES) {
		return (0);
	}

	/* Check if the message type is valid and extract additional data if
	 * applicable. */

	switch (imessage.hdr.type) {
	case (CMD_SET_INPUT):
		message->type = (MESSAGE_TYPE)imessage.hdr.type;
		message->data.fd = imessage.fd;
		break;
	case (CMD_EXIT):
	case (CMD_META):
	case (CMD_PLAY):
	case (CMD_PAUSE):
		message->type = (MESSAGE_TYPE)imessage.hdr.type;

		/* Set message->data to a dummy value. */

		message->data.fd = -1;
		break;
	default:
		child_fatalx("Invalid CMD_MESSAGE_TYPE received.");
	}

	imsg_free(&imessage);
	return (1);
}

static int
is_invalid_message_type(MESSAGE_TYPE type)
{
	return (type < 0 || MSG_SENTINEL <= type);
}
