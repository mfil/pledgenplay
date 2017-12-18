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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "child_errors.h"
#include "child_messages.h"

void
child_warn(const char *message)
{
	/* Enqueue a warning message to the parent, formatted similar to
	 * warn(3): The basename of the program, the message associated with
	 * the current errno, plus a message supplied by the caller. */

	char *full_message;
	if (asprintf(&full_message, "%s: %s: %s", getprogname(),
	    strerror(errno), message) == -1) {
		child_fatal("asprintf");
	}
	enqueue_message((u_int32_t)MSG_WARN, full_message);
}

void
child_warnx(const char *message)
{
	/* Like child_warn, but omit the message associated with errno. */

	char *full_message;
	if (asprintf(&full_message, "%s: %s", getprogname(), message) == -1) {
		child_fatal("asprintf");
	}
	enqueue_message((u_int32_t)MSG_WARN, full_message);
}

void
file_err(const char *message)
{
	char *full_message;
	if (asprintf(&full_message, "%s: %s: %s", getprogname(),
	    strerror(errno), message) == -1) {
		ipc_error("asprintf failed.");
	}
	enqueue_message((u_int32_t)MSG_FILE_ERR, full_message);
}

void
file_errx(const char *message)
{
	char *full_message;
	if (asprintf(&full_message, "%s: %s", getprogname(), message) == -1) {
		ipc_error("asprintf failed.");
	}
	enqueue_message((u_int32_t)MSG_FILE_ERR, full_message);
}

__dead void
child_fatal(const char *message)
{
	/* Send a fatal error message to the parent and exit. The message
	 * is formatted similar to err(3): The basename of the program, the
	 * message associated with the current errno, plus a message supplied
	 * by the caller. */

	char *full_message;
	if (asprintf(&full_message, "%s: %s: %s", getprogname(),
	    strerror(errno), message) == -1) {
		ipc_error("asprintf failed.");
	}
	enqueue_message((u_int32_t)MSG_WARN, full_message);
	send_messages();
	_exit(1);
}

__dead void
child_fatalx(const char *message)
{
	/* Like child_fatal, but omit the message associated with errno. */

	char *full_message;
	if (asprintf(&full_message, "%s: %s", getprogname(), message) == -1) {
		ipc_error("asprintf failed.");
	}
	enqueue_message((u_int32_t)MSG_FATAL, full_message);
	send_messages();
	_exit(1);
}

__dead void
ipc_error(const char *message)
{
	/* If the child process cannot communicate with the parent anymore,
	 * try to print an error message to stderr and exit. */
	dprintf(2, "pnp child: %s: %s\n", strerror(errno), message);
	_exit(1);
}
