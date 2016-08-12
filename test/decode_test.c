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

START_TEST (filetype_recognizes_flac)
{
	FILE	*f;
	f = fopen("./testdata/test.flac", "rb");
	assert(f != NULL);
	ck_assert_int_eq(filetype(f), FLAC);
	fclose(f);
}
END_TEST

START_TEST (filetype_recognizes_mp3)
{
	FILE	*f;
	f = fopen("./testdata/test.mp3", "rb");
	assert(f != NULL);
	ck_assert_int_eq(filetype(f), MP3);
	fclose(f);
}
END_TEST

START_TEST (filetype_recognizes_wav)
{
	FILE	*f;
	f = fopen("./testdata/test.wav", "rb");
	assert(f != NULL);
	ck_assert_int_eq(filetype(f), WAVE_PCM);
	fclose(f);
}
END_TEST

START_TEST (filetype_recognizes_unknown)
{
	FILE	*f;
	f = fopen("./testdata/random_garbage", "rb");
	assert(f != NULL);
	ck_assert_int_eq(filetype(f), UNKNOWN);
	fclose(f);
}
END_TEST

char	*files[] = {"./testdata/with_id3v2.wav", "./testdata/with_id3v2.flac","./testdata/with_id3v2.mp3"};
int	types[] = {WAVE_PCM, FLAC, MP3};
START_TEST (filetype_skips_id3v2)
{
	FILE	*f;
	f = fopen(files[_i], "rb");
	assert(f != NULL);
	ck_assert_int_eq(filetype(f), types[_i]);
	fclose(f);
}
END_TEST

/* Metadata extraction */

START_TEST (get_meta_returns_NULL_when_no_file_open)
{
	struct pollfd	pfd;
	struct imsgbuf	ibuf;
	struct meta	*mdata;
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
		child_main(sv, 0, -1);
	default:
		/* Parent process */
		close(sv[1]);
		imsg_init(&ibuf, sv[0]);
		pfd.fd = sv[0];
		pfd.events = POLLIN|POLLOUT;
		mdata = get_meta(&pfd, &ibuf);
		ck_assert_ptr_eq(mdata, NULL);
	}
}
END_TEST

START_TEST (get_meta_handles_vorbis_comment_in_flac)
{
	struct imsgbuf	ibuf;
	struct meta	*mdata;
	struct pollfd	pfd;
	pid_t		child_pid;
	int		fd, sv[2], nready;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		child_main(sv, 0, -1);
	default:
		/* Parent process */
		close(sv[1]);
		fd = open("./testdata/test.flac", O_RDONLY);
		if (fd == -1)
			err(1, "open");
		imsg_init(&ibuf, sv[0]);
		imsg_compose(&ibuf, (u_int32_t)NEW_FILE, 0, getpid(), fd, NULL,
		    0);
		pfd.fd = sv[0];
		pfd.events = POLLIN|POLLOUT;
		if ((nready = poll(&pfd, 1, 1)) == -1)
			err(1, "poll");
		if (nready == 0 || (pfd.revents & POLLOUT) == 0)
			errx(1, "Can't write to socket.");
		if (imsg_flush(&ibuf) == -1) {
			errx(1, "imsg_flush failed.");
		}
		mdata = get_meta(&pfd, &ibuf);
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
	struct imsgbuf	ibuf;
	struct meta	*mdata;
	struct pollfd	pfd;
	pid_t		child_pid;
	int		fd, sv[2], nready;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		child_main(sv, 0, -1);
	default:
		/* Parent process */
		close(sv[1]);
		fd = open("./testdata/with_id3v2.flac", O_RDONLY);
		if (fd == -1)
			err(1, "open");
		imsg_init(&ibuf, sv[0]);
		imsg_compose(&ibuf, (u_int32_t)NEW_FILE, 0, getpid(), fd, NULL,
		    0);
		pfd.fd = sv[0];
		pfd.events = POLLIN|POLLOUT;
		if ((nready = poll(&pfd, 1, 1)) == -1)
			err(1, "poll");
		if (nready == 0 || (pfd.revents & POLLOUT) == 0)
			errx(1, "Can't write to socket.");
		if (imsg_flush(&ibuf) == -1) {
			errx(1, "imsg_flush failed.");
		}
		mdata = get_meta(&pfd, &ibuf);
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
	struct imsgbuf	ibuf;
	struct pollfd	pfd;
	pid_t		child_pid;
	int		rv, cmp, sv[2], out_fd;

	if (socketpair(AF_UNIX, SOCK_STREAM, PF_LOCAL, sv) == -1)
		err(1, "socketpair");
	out_fd = open("./scratchspace/test.raw", O_WRONLY|O_CREAT|O_TRUNC,
	    S_IRUSR|S_IWUSR);
	if (out_fd == -1)
		err(1, "open");
	child_pid = fork();
	switch (child_pid) {
	case -1:
		err(1, "fork");
	case 0:
		/* Child process */
		child_main(sv, OUT_RAW, out_fd);
	default:
		/* Parent process */
		close(sv[1]);
		close(out_fd);
		imsg_init(&ibuf, sv[0]);
		pfd.fd = sv[0];
		pfd.events = POLLIN|POLLOUT;
		rv = decode("./testdata/test.flac", &pfd, &ibuf);
		ck_assert_int_eq(rv, 0);
		cmp = system("cmp ./testdata/test.raw ./scratchspace/test.raw 1>/dev/null");
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

	tcase_add_test(tc_filetype, filetype_recognizes_flac);
	tcase_add_test(tc_filetype, filetype_recognizes_mp3);
	tcase_add_test(tc_filetype, filetype_recognizes_wav);
	tcase_add_test(tc_filetype, filetype_recognizes_unknown);
	tcase_add_loop_test(tc_filetype, filetype_skips_id3v2, 0, 3);
	suite_add_tcase(s, tc_filetype);

	tcase_add_test(tc_meta, get_meta_returns_NULL_when_no_file_open);
	tcase_add_test(tc_meta, get_meta_handles_vorbis_comment_in_flac);
	tcase_add_test(tc_meta, get_meta_handles_id3v2_in_flac);
	suite_add_tcase(s, tc_meta);

	tcase_add_test(tc_dec, decode_converts_flac_to_raw);
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
