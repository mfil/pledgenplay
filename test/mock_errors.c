#include <stdlib.h>
#include <string.h>

#include "../child_errors.h"
#include "mock_errors.h"

static char *warn_msg = NULL;

void
ipc_error(const char *message)
{
	exit(IPC_ERROR_EXIT_CODE);
}

void
child_fatal(const char *message)
{
	exit(FATAL_EXIT_CODE);
}

void
child_fatalx(const char *message)
{
	exit(FATAL_EXIT_CODE);
}

void
child_warn(const char *message)
{
	if (warn_msg != NULL) {
		free(warn_msg);
	}
	warn_msg = strdup(message);
	child_warn_called = 1;
}

void
child_warnx(const char *message)
{
	if (warn_msg != NULL) {
		free(warn_msg);
	}
	warn_msg = strdup(message);
	child_warn_called = 1;
}

void
file_err(const char *message)
{
	file_err_called = 1;
}

void
file_errx(const char *message)
{
	file_err_called = 1;
}

char *
last_warn_message(void)
{
	return (warn_msg);
}
