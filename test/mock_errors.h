#ifndef MOCK_ERRORS_H
#define MOCK_ERRORS_H

#include <stdio.h>

int child_warn_called;
int file_err_called;

#define check_for_warning(instruction) \
	{ \
		child_warn_called = 0; \
		{ \
			instruction; \
		} \
		if (child_warn_called) { \
			dprintf(2, "warning: %s", last_warn_message()); \
		} \
		ck_assert_int_eq(child_warn_called, 0); \
	}

enum {
	IPC_ERROR_EXIT_CODE,
	FATAL_EXIT_CODE,
};

char *last_warn_message(void);

#endif
