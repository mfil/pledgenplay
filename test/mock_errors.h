int child_warn_called;
int file_err_called;

enum {
	IPC_ERROR_EXIT_CODE,
	FATAL_EXIT_CODE,
};

char *last_warn_message(void);
