/*
 * Copyright (c) 2016 Max Fillinger
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

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <assert.h>
#include <check.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "comm.h"
#include "file.h"
#include "pnp.h"
/* Filetype detection */

START_TEST (filetype_identifies_files)
{
	char	*files[4] = {"./testdata/test.wav", "./testdata/test.flac",
	    "./testdata/test.mp3", "./testdata/random_garbage"};
	int	types[4] = {WAVE_PCM, FLAC, MP3, UNKNOWN};
	int	fd;
	fd = open(files[_i], O_RDONLY);
	assert(fd != -1);
	ck_assert_int_eq(filetype(fd), types[_i]);
	close(fd);
}
END_TEST

START_TEST (filetype_skips_id3v2)
{
	char	*files[] = {"./testdata/with_id3v2.wav",
	    "./testdata/with_id3v2.flac", "./testdata/with_id3v2.mp3"};
	int	types[] = {WAVE_PCM, FLAC, MP3};
	int	fd;

	fd = open(files[_i], O_RDONLY);
	assert(fd != -1);
	ck_assert_int_eq(filetype(fd), types[_i]);
	close(fd);
}
END_TEST

/* Metadata extraction */

START_TEST (get_meta_returns_NULL_when_no_file_open)
{
	struct meta	*mdata;
	struct out	out;
	int		sv[2];
	pid_t		child_pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		out.type = OUT_RAW;
		out.handle.fp = NULL;
		child_main(sv, &out);
	default:
		/* Parent process */
		parent_init(sv, child_pid);
		mdata = get_meta();
		ck_assert_ptr_eq(mdata, NULL);
	}
}
END_TEST

START_TEST (get_meta_handles_vorbis_comment_in_flac)
{
	struct meta	*mdata;
	struct out	out;
	pid_t		child_pid;
	int		sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		out.type = OUT_RAW;
		out.handle.fp = NULL;
		child_main(sv, &out);
	default:
		/* Parent process */
		parent_init(sv, child_pid);
		if (send_new_file("./testdata/test.flac"))
			errx(1, "send_new_file: file rejected");
		mdata = get_meta();
		ck_assert_ptr_ne(mdata, NULL);
		ck_assert_ptr_ne(mdata->time, NULL);
		ck_assert_str_eq(mdata->time, "0:41");
		ck_assert_ptr_ne(mdata->artist, NULL);
		ck_assert_str_eq(mdata->artist, "sunnata");
		ck_assert_ptr_ne(mdata->title, NULL);
		ck_assert_str_eq(mdata->title, "I");
		ck_assert_ptr_ne(mdata->album, NULL);
		ck_assert_str_eq(mdata->album,
		    "Climbing the colossus (cd ed.)");
		ck_assert_int_eq(mdata->trackno, 1);
		ck_assert_ptr_ne(mdata->date, NULL);
		ck_assert_str_eq(mdata->date, "2014");
	}
}
END_TEST

START_TEST (get_meta_handles_id3v2_in_flac)
{
	struct meta	*mdata;
	struct out	out;
	pid_t		child_pid;
	int		sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		out.type = OUT_RAW;
		out.handle.fp = NULL;
		child_main(sv, &out);
	default:
		/* Parent process */
		parent_init(sv, child_pid);
		if (send_new_file("./testdata/with_id3v2.flac"))
			errx(1, "send_new_file: file rejected");
		mdata = get_meta();
		ck_assert_ptr_ne(mdata, NULL);
		ck_assert_ptr_ne(mdata->time, NULL);
		ck_assert_str_eq(mdata->time, "0:41");
		ck_assert_ptr_ne(mdata->artist, NULL);
		ck_assert_str_eq(mdata->artist, "sunnata");
		ck_assert_ptr_ne(mdata->title, NULL);
		ck_assert_str_eq(mdata->title, "I");
		ck_assert_ptr_ne(mdata->album, NULL);
		ck_assert_str_eq(mdata->album,
		    "Climbing the colossus (cd ed.)");
		ck_assert_int_eq(mdata->trackno, 1);
		ck_assert_ptr_ne(mdata->date, NULL);
		ck_assert_str_eq(mdata->date, "2014");
	}
}
END_TEST

START_TEST (decode_converts_flac_to_raw)
{
	struct out	out;
	FILE		*outfp;
	pid_t		child_pid;
	int		rv, cmp, sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	outfp = fopen("./scratchspace/test.raw", "w");
	if (outfp == NULL)
		err(1, "fopen");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		out.type = OUT_RAW;
		out.handle.fp = outfp;
		child_main(sv, &out);
	default:
		/* Parent process */
		parent_init(sv, child_pid);
		rv = decode("./testdata/test.flac");
		ck_assert_int_eq(rv, 0);
		cmp = system("cmp ./testdata/test.raw ./scratchspace/test.raw 1>/dev/null");
		ck_assert_int_eq(cmp, 0);
	}
}
END_TEST

START_TEST (test_write_wav_header)
{
	FILE	*f = fopen("scratchspace/wav_hdr", "w");
	int	bytes_written, cmp;

	assert(f != NULL);
	bytes_written = write_wav_header(f, 2, 44100, 16, 0);
	assert(fclose(f) != EOF);
	ck_assert_int_eq(bytes_written, 44);
	cmp = system("cmp ./testdata/wav_hdr ./scratchspace/wav_hdr 1>/dev/null");
	ck_assert_int_eq(cmp, 0);
}
END_TEST

START_TEST (decode_converts_flac_to_wav)
{
	struct out	out;
	FILE		*outfp;
	pid_t		child_pid;
	int		rv, cmp, sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	outfp = fopen("./scratchspace/test.wav", "w");
	if (outfp == NULL)
		err(1, "fopen");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		out.type = OUT_WAV_FILE;
		out.handle.fp = outfp;
		child_main(sv, &out);
	default:
		/* Parent process */
		parent_init(sv, child_pid);
		rv = decode("./testdata/test.flac");
		ck_assert_int_eq(rv, 0);
		cmp = system("cmp ./testdata/test.wav ./scratchspace/test.wav"
		    " 1>/dev/null");
		ck_assert_int_eq(cmp, 0);
	}
}
END_TEST

Suite
*decode_suite(void)
{
	Suite *s;
	TCase *tc_filetype, *tc_meta, *tc_dec;

	s = suite_create("Decode");
	tc_filetype = tcase_create("Filetype detection");
	tc_meta = tcase_create("Metadata extraction");
	tc_dec = tcase_create("Decoding");

	tcase_add_loop_test(tc_filetype, filetype_identifies_files, 0, 4);
	tcase_add_loop_test(tc_filetype, filetype_skips_id3v2, 0, 3);
	suite_add_tcase(s, tc_filetype);

	tcase_add_test(tc_meta, get_meta_returns_NULL_when_no_file_open);
	tcase_add_test(tc_meta, get_meta_handles_vorbis_comment_in_flac);
	tcase_add_test(tc_meta, get_meta_handles_id3v2_in_flac);
	suite_add_tcase(s, tc_meta);

	tcase_add_test(tc_dec, decode_converts_flac_to_raw);
	tcase_add_test(tc_dec, test_write_wav_header);
	tcase_add_test(tc_dec, decode_converts_flac_to_wav);
	suite_add_tcase(s, tc_dec);
	
	return (s);
}

int
main(void)
{
	int	no_failed;
	Suite	*s;
	SRunner	*sr;

	s = decode_suite();
	sr = srunner_create(s);

	srunner_run_all(sr, CK_NORMAL);
	no_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return ((no_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE);
}
