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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mock_errors.h"

#include "../decoder.h"
#include "../output.h"

START_TEST (output_raw_can_write_to_file)
{
	int fd = open("scratchspace/file", O_RDWR | O_TRUNC | O_CREAT,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd < 0) {
		err(1, "open");
	}

	const struct output *out = output_raw(fd);
	ck_assert_ptr_ne(out, OUTPUT_INIT_OK);

	char *teststr = "Hello, world!";
	struct decoded_frame testframe = { teststr, strlen(teststr) + 1,
	    0, 0, 0};
	ck_assert_int_ne(out->ready_for_new_frame(), 0);
	out->next_frame(&testframe);
	OUTPUT_RUN_STATUS status;
	do {
		status = out->run();
		ck_assert_int_ne(status, OUTPUT_ERROR);
	} while (status == OUTPUT_BUSY);

	lseek(fd, 0, SEEK_SET);
	char compare[strlen(teststr) + 1];
	read(fd, compare, strlen(teststr) + 1);
	ck_assert_str_eq(teststr, compare);

	check_for_warning(out->close());
}
END_TEST

START_TEST (output_wav_writes_wav_header)
{
	int fd = open("scratchspace/wav_hdr", O_RDWR | O_TRUNC | O_CREAT,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (fd < 0) {
		err(1, "open");
	}

	/* Set dummy parameters: 0 samples, 2 channels, 16 bit, 44.1 khz. */

	struct audio_parameters params = {0, 2, 16, 44100, 0};

	const struct output *out = output_wav(fd, &params);
	ck_assert_ptr_ne(out, NULL);

	struct decoded_frame testframe = {NULL, 0, 0, 0, 0};
	ck_assert_int_ne(out->ready_for_new_frame(), 0);
	out->next_frame(&testframe);
	OUTPUT_RUN_STATUS status;
	do {
		status = out->run();
		ck_assert_int_ne(status, OUTPUT_ERROR);
	} while (status == OUTPUT_BUSY);

	check_for_warning(out->close());

	int cmp_rv = system("cmp scratchspace/wav_hdr testdata/wav_hdr");
	ck_assert_int_eq(cmp_rv, 0);
}
END_TEST

Suite
*decoder_suite(void)
{
	Suite *s = suite_create("Test file output");

	TCase *tc_writing = tcase_create("writing");
	tcase_add_test(tc_writing, output_raw_can_write_to_file);
	tcase_add_test(tc_writing, output_wav_writes_wav_header);

	suite_add_tcase(s, tc_writing);

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
