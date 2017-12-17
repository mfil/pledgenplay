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
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../decoder.h"
#include "../output.h"

START_TEST (decoding_to_raw_audio_data_works)
{
	/* Initialize output. */

	char *output_filename = "scratchspace/test.raw";
	int out_fd = open(output_filename, O_WRONLY, O_CREAT | O_TRUNC,
	    S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	if (out_fd < 0) {
		err(1, "open");
	}
	struct output out = output_raw(out_fd);

	/* Initialize decoder. */

	char *input_filename = "testdata/test.flac";
	int in_fd = open(input_filename, O_RDONLY);
	DECODER_INIT_STATUS init_status = decoder_initialize(in_fd);
	ck_assert_int_eq(init_status, DECODER_INIT_OK);

	/* Setupt the decoding loop. */

	struct pollfd *pollfd;
	nfds_t num_pollfds = out.num_pollfds();
	pollfd = malloc(num_pollfds);
	if (pollfd == NULL) {
		err(1, "malloc");
	}

	int decoder_finished = 0;
	int output_finished = 0;
	struct decoded_frame *frame = NULL;
	while (! decoder_finished && ! output_finished) {
		out.set_pollfds(pollfd);
		int poll_status = poll(pollfd, num_pollfds, 0);
		if (poll_status == -1) {
			err(1, "poll");
		}
		if (poll_status == 0) {
			continue;
		}
		out.check_pollfds(pollfd);

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

	free(pollfd);
	close(in_fd);
	close(out_fd);

	char *decoded_file = "testdata/test.raw";
	char *command;
	asprintf(&command, "cmp %s %s", output_filename, decoded_file);
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

	TCase *tc_decode_to_raw = tcase_create("raw");
	tcase_add_test(tc_decode_to_raw, decoding_to_raw_audio_data_works);

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
