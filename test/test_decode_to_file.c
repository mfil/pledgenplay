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

#include <check.h>

#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mock_errors.h"

#include "../decoder.h"
#include "../input_file.h"
#include "../output.h"

static void
new_input_from_filename(const char *filename)
{
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	NEW_FILE_STATUS status = set_new_input_file(fd);
	if (status != NEW_FILE_OK) {
		errx(1, "error in set_new_input_file");
	}
}

START_TEST (decoding_to_file_works)
{
	/* Initialize input and decoder. */

	char *input_filename = "testdata/test.flac";
	new_input_from_filename(input_filename);
	DECODER_INIT_STATUS dec_init_status = decoder_initialize();
	ck_assert_int_eq(dec_init_status, DECODER_INIT_OK);

	/* Initialize output. */

	char *output_filenames[2] = {"scratchspace/test.raw",
	    "scratchspace/test.wav"};
	int out_fd = open(output_filenames[_i], O_WRONLY, O_CREAT | O_TRUNC,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (out_fd < 0) {
		err(1, "open");
	}
	struct output out;
	OUTPUT_INIT_STATUS out_init_status = -1;
	if (_i == 0) {
		out_init_status = output_raw(out_fd, &out);
	}
	else if (_i == 1) {
		struct audio_parameters const *params;
		params = decoder_get_parameters();
		ck_assert_ptr_ne(params, NULL);
		out_init_status = output_wav(out_fd, params, &out);
	}
	ck_assert_int_eq(out_init_status, OUTPUT_INIT_OK);

	/* Run the decoding loop. */

	int decoder_finished = 0;
	int output_finished = 0;
	struct decoded_frame *frame = NULL;
	while (! decoder_finished && ! output_finished) {
		if (! decoder_finished && out.ready_for_new_frame()) {
			DECODER_DECODE_STATUS status;
			free_decoded_frame(frame);
			status = decoder_decode_next_frame(&frame);
			if (status == DECODER_DECODE_OK) {
				ck_assert_ptr_ne(frame, NULL);
				out.next_frame(frame);
			}
			else if (status == DECODER_DECODE_FINISHED) {
				decoder_finished = 1;
			}
			else if (status == DECODER_DECODE_ERROR) {
				errx(1, "decoding error");
			}
			else {
				ck_assert(0 == 1);
			}
		}

		OUTPUT_RUN_STATUS status = out.run();
		if (decoder_finished && status == OUTPUT_IDLE) {
			output_finished = 1;
		}
		else if (status == OUTPUT_ERROR) {
			errx(1, "output error");
		}
	}

	input_file_close();
	check_for_warning(out.close());

	char *decoded_files[2] = {"testdata/test.raw", "testdata/test.wav"};
	char *command;
	asprintf(&command, "cmp %s %s", output_filenames[_i],
	    decoded_files[_i]);
	if (command == NULL) {
		err(1, "asprintf");
	}
	int cmp_rv = system(command);
	free(command);
	ck_assert_int_eq(cmp_rv, 0);
}
END_TEST

Suite
*decoder_suite(void)
{
	Suite *s = suite_create("Test decoding to file");

	TCase *tc_decode_to_raw = tcase_create("decoding to file");
	tcase_add_loop_test(tc_decode_to_raw, decoding_to_file_works, 0, 2);

	suite_add_tcase(s, tc_decode_to_raw);

	return (s);
}

int
main(void)
{
	int	no_failed;
	Suite	*s;
	SRunner	*sr;

	s = decoder_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	no_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return ((no_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}
