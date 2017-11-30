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

#include "../decoder.h"
#include "mock_errors.h"

OUTPUT_WRITE_STATUS
dummy_write_cb(void *buf, size_t bytes_to_write, size_t *bytes_written)
{
	if (bytes_written != NULL) {
		*bytes_written = bytes_to_write;
	}
	return (OUTPUT_WRITE_OK);
}

START_TEST (decoder_accepts_flac_file)
{
	int fd = open("testdata/test.flac", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}

	DECODER_INIT_STATUS status = decoder_initialize(fd, dummy_write_cb);
	ck_assert_int_eq(status, DECODER_INIT_OK);
}
END_TEST

START_TEST (decoder_extracts_id3v2_metadata) {
	int fd = open("testdata/with_id3v2.flac", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}

	child_warn_called = 0;
	DECODER_INIT_STATUS status = decoder_initialize(fd, dummy_write_cb);
	ck_assert_int_eq(status, DECODER_INIT_OK);
	if (child_warn_called) {
		child_warn_called = 0;
		dprintf(2, "%s\n", last_warn_message());
	}

	struct metadata const *mdata;
	mdata = decoder_get_metadata();
	ck_assert_ptr_ne(mdata, NULL);
	ck_assert_ptr_ne(mdata->artist, NULL);
	ck_assert_str_eq(mdata->artist, "sunnata");
	ck_assert_ptr_ne(mdata->title, NULL);
	ck_assert_str_eq(mdata->title, "I");
	ck_assert_ptr_ne(mdata->album, NULL);
	ck_assert_str_eq(mdata->album, "Climbing the colossus (cd ed.)");
	ck_assert_ptr_ne(mdata->trackno, NULL);
	ck_assert_str_eq(mdata->trackno, "1");
	ck_assert_ptr_ne(mdata->date, NULL);
	ck_assert_str_eq(mdata->date, "2014");
	ck_assert_ptr_ne(mdata->time, NULL);
	ck_assert_str_eq(mdata->time, "0:41");
}
END_TEST

Suite
*decoder_suite(void)
{
	Suite *s = suite_create("Test decoder");

	TCase *tc_init = tcase_create("initialization");
	tcase_add_test(tc_init, decoder_accepts_flac_file);

	TCase *tc_metadata = tcase_create("metadata");
	tcase_add_test(tc_metadata, decoder_extracts_id3v2_metadata);

	suite_add_tcase(s, tc_init);
	suite_add_tcase(s, tc_metadata);

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
