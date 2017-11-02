#include <stdlib.h>

#include "../child_errors.h"
#include "mock_errors.h"

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
	child_warn_called = 1;
}

void
child_warnx(const char *message)
{
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
