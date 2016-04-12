#include <sys/socket.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/uio.h>

#include <assert.h>
#include <check.h>
#include <err.h>
#include <fcntl.h>
#include <imsg.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include "comm.h"
#include "file.h"
#include "pnp.h"

/* Filetype detection */

START_TEST (filetype_recognizes_flac)
{
	int	fd;
	fd = open("./testdata/test.flac", O_RDONLY);
	assert(fd != -1);
	ck_assert_int_eq(filetype(fd), FLAC);
	close(fd);
}
END_TEST

START_TEST (filetype_recognizes_mp3)
{
	int	fd;
	fd = open("./testdata/test.mp3", O_RDONLY);
	assert(fd != -1);
	ck_assert_int_eq(filetype(fd), MP3);
	close(fd);
}
END_TEST

START_TEST (filetype_recognizes_wav)
{
	int	fd;
	fd = open("./testdata/test.wav", O_RDONLY);
	assert(fd != -1);
	ck_assert_int_eq(filetype(fd), WAVE_PCM);
	close(fd);
}
END_TEST

START_TEST (filetype_recognizes_unknown)
{
	int	fd;
	fd = open("./testdata/random_garbage", O_RDONLY);
	assert(fd != -1);
	ck_assert_int_eq(filetype(fd), UNKNOWN);
	close(fd);
}
END_TEST

char	*files[] = {"./testdata/with_id3v2.wav", "./testdata/with_id3v2.flac","./testdata/with_id3v2.mp3"};
int	types[] = {WAVE_PCM, FLAC, MP3};
START_TEST (filetype_skips_id3v2)
{
	int	fd;
	fd = open(files[_i], O_RDONLY);
	assert(fd != -1);
	ck_assert_int_eq(filetype(fd), types[_i]);
	close(fd);
}
END_TEST

START_TEST (filetype_returns_min_one_on_inval_fd)
{
	int	fd = 23;
	ck_assert_int_eq(filetype(fd), -1);
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

Suite
*decode_suite(void)
{
	Suite *s;
	TCase *tc_filetype, *tc_meta;

	s = suite_create("Decode");
	tc_filetype = tcase_create("Filetype detection");
	tc_meta = tcase_create("Metadata extraction");

	tcase_add_test(tc_filetype, filetype_recognizes_flac);
	tcase_add_test(tc_filetype, filetype_recognizes_mp3);
	tcase_add_test(tc_filetype, filetype_recognizes_wav);
	tcase_add_test(tc_filetype, filetype_recognizes_unknown);
	tcase_add_loop_test(tc_filetype, filetype_skips_id3v2, 0, 3);
	tcase_add_test(tc_filetype, filetype_returns_min_one_on_inval_fd);
	suite_add_tcase(s, tc_filetype);

	tcase_add_test(tc_meta, get_meta_returns_NULL_when_no_file_open);
	tcase_add_test(tc_meta, get_meta_handles_vorbis_comment_in_flac);
	tcase_add_test(tc_meta, get_meta_handles_id3v2_in_flac);
	suite_add_tcase(s, tc_meta);
	
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
