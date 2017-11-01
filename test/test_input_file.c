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

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../input_file.h"

/* Mock error functions. */

static int child_warn_called = 0;
static int file_err_called = 0;

enum {
	IPC_ERROR_EXIT_CODE,
	FATAL_EXIT_CODE,
};

void
ipc_error(const char *message) {
	exit(IPC_ERROR_EXIT_CODE);
}

void
child_fatal(const char *message) {
	exit(FATAL_EXIT_CODE);
}

void
child_fatalx(const char *message) {
	exit(FATAL_EXIT_CODE);
}

void
child_warn(const char *message) {
	child_warn_called = 1;
}

void
child_warnx(const char *message) {
	child_warn_called = 1;
}

void
file_err(const char *message) {
	file_err_called = 1;
}

void
file_errx(const char *message) {
	file_err_called = 1;
}

START_TEST(input_file_determines_filetype)
{
	char *testfiles[3] = {"testdata/test.flac", "testdata/test.mp3",
	    "testdata/test.wav"};
	int filetypes[3] = {FLAC, MP3, WAVE_PCM};
	int fd = open(testfiles[_i], O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	NEW_FILE_STATUS status = set_new_input_file(fd);
	ck_assert_int_eq(status, NEW_FILE_OK);
	ck_assert_int_ne(input_file_is_open(), 0);
	ck_assert_int_eq(input_file_has_id3v2_tag(), 0);
	ck_assert_int_eq(input_file_get_type(), filetypes[_i]);

	child_warn_called = 0;
	input_file_close();
	ck_assert_int_eq(child_warn_called, 0);
}
END_TEST

START_TEST(input_file_detects_id3v2_tags)
{
	char *testfiles[3] = {"testdata/with_id3v2.flac",
	    "testdata/with_id3v2.mp3", "testdata/with_id3v2.wav"};
	int filetypes[3] = {FLAC, MP3, WAVE_PCM};
	int fd = open(testfiles[_i], O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	NEW_FILE_STATUS status = set_new_input_file(fd);
	ck_assert_int_eq(status, NEW_FILE_OK);
	ck_assert_int_ne(input_file_is_open(), 0);
	ck_assert_int_ne(input_file_has_id3v2_tag(), 0);
	ck_assert_int_eq(input_file_get_type(), filetypes[_i]);

	child_warn_called = 0;
	input_file_close();
	ck_assert_int_eq(child_warn_called, 0);
}
END_TEST

START_TEST(input_file_calls_file_err_on_invalid_file)
{
	int fd = open("testdata/random_garbage", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	file_err_called = 0;
	NEW_FILE_STATUS status = set_new_input_file(fd);
	ck_assert_int_eq(status, NEW_FILE_FAILURE);
	ck_assert_int_eq(file_err_called, 1);
	ck_assert_int_eq(input_file_is_open(), 0);
}
END_TEST

START_TEST(input_file_read_and_seek_works)
{
	const size_t TEST_BUF_SIZE = 100;
	int fd = open("testdata/test.flac", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	NEW_FILE_STATUS new_file_status = set_new_input_file(fd);
	ck_assert_int_eq(new_file_status, NEW_FILE_OK);

	/* Read the first bytes. */

	char test_buf[TEST_BUF_SIZE];
	READ_STATUS read_status = input_file_read(test_buf, sizeof(test_buf),
	    NULL);
	ck_assert_int_eq(read_status, READ_OK);

	/* Rewind and compare. */

	SEEK_STATUS seek_status = input_file_rewind();
	ck_assert_int_eq(seek_status, SEEK_OK);
	char compare_buf[TEST_BUF_SIZE];
	read_status = input_file_read(compare_buf, sizeof(compare_buf), NULL);
	ck_assert_int_eq(read_status, READ_OK);
	ck_assert_int_eq(memcmp(test_buf, compare_buf, TEST_BUF_SIZE), 0);

	/* Seek deeper into the file and repeat the experiment. */

	seek_status = input_file_seek((long) 2 * TEST_BUF_SIZE);
	ck_assert_int_eq(seek_status, SEEK_OK);
	read_status = input_file_read(test_buf, sizeof(test_buf), NULL);
	ck_assert_int_eq(read_status, READ_OK);

	seek_status = input_file_seek(-((long) TEST_BUF_SIZE));
	ck_assert_int_eq(seek_status, SEEK_OK);
	read_status = input_file_read(compare_buf, sizeof(compare_buf), NULL);
	ck_assert_int_eq(read_status, READ_OK);
	ck_assert_int_eq(memcmp(test_buf, compare_buf, TEST_BUF_SIZE), 0);

	input_file_close();
}
END_TEST

Suite
*child_messages_suite(void)
{
	Suite *s = suite_create("Test input_file");

	TCase *tc_filetype = tcase_create("filetype detection");
	tcase_add_loop_test(tc_filetype, input_file_determines_filetype,0, 3);
	tcase_add_loop_test(tc_filetype, input_file_detects_id3v2_tags, 0, 3);
	tcase_add_test(tc_filetype, input_file_calls_file_err_on_invalid_file);

	TCase *tc_read_seek = tcase_create("read and seek"); 
	tcase_add_test(tc_read_seek, input_file_read_and_seek_works);

	suite_add_tcase(s, tc_filetype);
	suite_add_tcase(s, tc_read_seek);

	return (s);
}

int
main(void)
{
	int	no_failed;
	Suite	*s;
	SRunner	*sr;

	s = child_messages_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	no_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return ((no_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}
