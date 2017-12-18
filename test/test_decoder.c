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

#include "../decoder.h"
#include "mock_errors.h"

START_TEST (decoder_accepts_flac_file)
{
	int fd = open("testdata/test.flac", O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}

	DECODER_INIT_STATUS status = decoder_initialize(fd);
	ck_assert_int_eq(status, DECODER_INIT_OK);
}
END_TEST

START_TEST (decoder_extracts_metadata) {
	const char *filenames[2] = { "testdata/test.flac",
	    "testdata/with_id3v2.flac" };
	int fd = open(filenames[_i], O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}

	DECODER_INIT_STATUS status;
	check_for_warning(status = decoder_initialize(fd));
	ck_assert_int_eq(status, DECODER_INIT_OK);

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

START_TEST (decoder_extracts_audio_params)
{
	const char *filename = "testdata/test.flac";
	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		err(1, "open");
	}

	DECODER_INIT_STATUS status;
	check_for_warning(status = decoder_initialize(fd));
	ck_assert_int_eq(status, DECODER_INIT_OK);

	struct audio_parameters const *params = decoder_get_parameters();
	ck_assert_ptr_ne(params, NULL);
	ck_assert_int_eq(params->total_samples, 1844556);
	ck_assert_int_eq(params->channels, 2);
	ck_assert_int_eq(params->bits_per_sample, 16);
	ck_assert_int_eq(params->rate, 44100);
}
END_TEST

START_TEST (decoder_decodes_flac)
{
	/* Decode a flac file to raw audio data and compare it to a file
	 * that was produced by the standard flac tools. */

	const char input_filename[] = "testdata/test.flac";
	const char output_filename[] = "scratchspace/test.raw";
	const char compare_filename[] = "testdata/test.raw";

	/* Initialize the decoder. */

	int in_fd = open(input_filename, O_RDONLY);
	if (in_fd < 0) {
		err(1, "open");
	}
	DECODER_INIT_STATUS status = decoder_initialize(in_fd);
	ck_assert_int_eq(status, DECODER_INIT_OK);

	/* Decode the file. */

	FILE *out = fopen(output_filename, "w");
	if (out == NULL) {
		err(1, "fopen");
	}
	int decoder_finished = 0;
	while (! decoder_finished) {
		DECODER_DECODE_STATUS status;
		struct decoded_frame *frame;
		status = decoder_decode_next_frame(&frame);
		if (status == DECODER_DECODE_FINISHED) {
			decoder_finished = 1;
		}
		else if (status == DECODER_DECODE_ERROR) {
			errx(1, "decoder_run");
		}
		if (frame != NULL) {
			fwrite(frame->data, 1, frame->length, out);
		}
		free_decoded_frame(frame);
	}
	close(in_fd);
	fclose(out);

	char *command;
	asprintf(&command, "cmp \"%s\" \"%s\"", output_filename,
	    compare_filename);
	if (command == NULL) {
		err(1, "asprintf");
	}
	int cmp_rv = system(command);
	ck_assert_int_eq(cmp_rv, 0);
}
END_TEST

Suite
*decoder_suite(void)
{
	Suite *s = suite_create("Test decoder");

	TCase *tc_init = tcase_create("initialization");
	tcase_add_test(tc_init, decoder_accepts_flac_file);
	tcase_add_loop_test(tc_init, decoder_extracts_metadata, 0, 2);
	tcase_add_test(tc_init, decoder_extracts_audio_params);

	TCase *tc_decode = tcase_create("decode");
	tcase_add_test(tc_decode, decoder_decodes_flac);

	suite_add_tcase(s, tc_init);
	suite_add_tcase(s, tc_decode);

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
