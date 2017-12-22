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

#ifndef PNP_CHILD_MESSAGES_H
#define PNP_CHILD_MESSAGES_H

#include "message_types.h"

typedef enum {
	SEND_MSG_OK,
	SEND_MSG_SOCKET_NOT_READY,
} SEND_MSG_STATUS;

typedef enum {
	NO_MESSAGES,
	GOT_MESSAGE,
} GET_NEXT_MSG_STATUS;

union message_data {
	int fd;
};

struct message {
	CMD_MESSAGE_TYPE type;
	union message_data data;
};

void initialize_ipc(int);
SEND_MSG_STATUS send_messages(void);
void check_for_messages(void);
void enqueue_message(MESSAGE_TYPE, const char *);
GET_NEXT_MSG_STATUS get_next_message(struct message *);

#endif
