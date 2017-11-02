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
#include "mock_errors.h"

static void
set_new_file_and_check(int fd)
{
	NEW_FILE_STATUS status = set_new_input_file(fd);
	ck_assert_int_eq(status, NEW_FILE_OK);
	ck_assert(input_file_is_open());
}

static void
close_and_check()
{
	child_warn_called = 0;
	input_file_close();
	ck_assert(!child_warn_called);
}

static void
read_and_check(void *buf, size_t length)
{
	size_t bytes_read;
	READ_STATUS status = input_file_read(buf, length, &bytes_read);
	ck_assert_int_eq(status, READ_OK);
	ck_assert(length == bytes_read);
}

static void
rewind_and_check(void)
{
	SEEK_STATUS status = input_file_rewind();
	ck_assert_int_eq(status, SEEK_OK);
}

static void
seek_and_check(long offset)
{
	SEEK_STATUS status = input_file_seek(offset);
	ck_assert_int_eq(status, SEEK_OK);
}

START_TEST (input_file_determines_filetype)
{
	char *testfiles[3] = {"testdata/test.flac", "testdata/test.mp3",
	    "testdata/test.wav"};
	int filetypes[3] = {FLAC, MP3, WAVE_PCM};
	int fd = open(testfiles[_i], O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	set_new_file_and_check(fd);
	ck_assert(!input_file_has_id3v2_tag());
	ck_assert_int_eq(input_file_get_type(), filetypes[_i]);

	close_and_check();
}
END_TEST

START_TEST (input_file_detects_id3v2_tags)
{
	char *testfiles[3] = {"testdata/with_id3v2.flac",
	    "testdata/with_id3v2.mp3", "testdata/with_id3v2.wav"};
	int filetypes[3] = {FLAC, MP3, WAVE_PCM};
	int fd = open(testfiles[_i], O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	set_new_file_and_check(fd);
	ck_assert(input_file_has_id3v2_tag());
	ck_assert_int_eq(input_file_get_type(), filetypes[_i]);

	close_and_check();
}
END_TEST

START_TEST (input_file_calls_file_err_on_invalid_file)
{
	int fd = open("testdata/random_garbage", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	file_err_called = 0;
	NEW_FILE_STATUS status = set_new_input_file(fd);
	ck_assert_int_eq(status, NEW_FILE_FAILURE);
	ck_assert(file_err_called);
	ck_assert(!input_file_is_open());
}
END_TEST

START_TEST (input_file_read_and_seek_works)
{
	const size_t TEST_BUF_SIZE = 100;
	int fd = open("testdata/test.flac", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	set_new_file_and_check(fd);

	/* Read the first bytes. */

	char test_buf[TEST_BUF_SIZE];
	read_and_check(test_buf, sizeof(test_buf));

	/* Rewind and compare. */

	rewind_and_check();
	char compare_buf[TEST_BUF_SIZE];
	read_and_check(compare_buf, sizeof(compare_buf));
	ck_assert_int_eq(memcmp(test_buf, compare_buf, TEST_BUF_SIZE), 0);

	/* Seek deeper into the file and repeat the experiment. */

	seek_and_check((long)(2 * TEST_BUF_SIZE));
	read_and_check(test_buf, sizeof(test_buf));

	seek_and_check(-((long)TEST_BUF_SIZE));
	read_and_check(compare_buf, sizeof(compare_buf));
	ck_assert_int_eq(memcmp(test_buf, compare_buf, TEST_BUF_SIZE), 0);

	close_and_check();
}
END_TEST

START_TEST (no_read_after_closing)
{
	int fd = open("testdata/test.flac", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	set_new_file_and_check(fd);

	close_and_check();

	READ_STATUS status = input_file_read(NULL, 0, NULL);
	ck_assert_int_eq(status, READ_NO_FILE);
}
END_TEST

START_TEST (no_seek_after_closing)
{
	int fd = open("testdata/test.flac", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}
	set_new_file_and_check(fd);

	close_and_check();

	SEEK_STATUS status = input_file_rewind();
	ck_assert_int_eq(status, SEEK_NO_FILE);
	status = input_file_seek(0);
	ck_assert_int_eq(status, SEEK_NO_FILE);
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
	tcase_add_test(tc_read_seek, no_read_after_closing);
	tcase_add_test(tc_read_seek, no_seek_after_closing);

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
