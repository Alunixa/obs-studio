/* Exercise the real muxer's private cleanup and I/O paths without a GPU.
 * Only the test target overrides fclose to inject a final-flush failure. */
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>

static bool fail_close;
static int test_fclose(FILE *file)
{
	int result = fclose(file);
	if (fail_close) {
		errno = ENOSPC;
		return EOF;
	}
	return result;
}

#define fclose test_fclose
#define main replay_mux_main
#define wmain replay_mux_wmain
#include "../../plugins/obs-ffmpeg/ffmpeg-mux/ffmpeg-mux.c"
#undef wmain
#undef main
#undef fclose

#include <util/bmem.h>

#define CHECK(condition)                                                                 \
	do {                                                                             \
		if (!(condition)) {                                                       \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			exit(1);                                                         \
		}                                                                        \
	} while (0)

static void test_unopened_output(void)
{
	struct ffmpeg_mux mux = {0};
	mux.output = avformat_alloc_context();
	CHECK(mux.output != NULL);
	mux.output->oformat = av_guess_format("matroska", NULL, NULL);
	CHECK(mux.output->oformat != NULL);
	CHECK(mux.output->pb == NULL);
	CHECK(ffmpeg_mux_free(&mux));
	CHECK(ffmpeg_mux_free(&mux));
	puts("PASS failed output initialization with no AVIO context; repeated cleanup");
}

static void test_io(const char *path, int mode)
{
	struct ffmpeg_mux mux = {0};
	FILE *seed = os_fopen(path, "wb");
	CHECK(seed != NULL && fclose(seed) == 0);
	mux.io.output_file = os_fopen(path, mode == 1 ? "rb" : "wb");
	CHECK(mux.io.output_file != NULL);
	dstr_copy(&mux.params.printable_file, "synthetic-mux-output");
	CHECK(pthread_mutex_init(&mux.io.data_mutex, NULL) == 0);
	mux.io.mutex_initialized = true;
	CHECK(os_event_init(&mux.io.buffer_space_available_event, OS_EVENT_TYPE_AUTO) == 0);
	CHECK(os_event_init(&mux.io.new_data_available_event, OS_EVENT_TYPE_AUTO) == 0);
	CHECK(pthread_create(&mux.io.io_thread, NULL, ffmpeg_mux_io_thread, &mux) == 0);
	mux.io.active = true;
	uint8_t bytes[4096] = {0x5a};
	if (mode == 2)
		mux.io.next_pos = UINT64_MAX;
	fail_close = mode == 3;
	CHECK(ffmpeg_mux_write_av_buffer(&mux, bytes, sizeof(bytes)) == sizeof(bytes));
	CHECK(ffmpeg_mux_free(&mux) == (mode == 0));
	fail_close = false;
	CHECK(os_unlink(path) == 0);
}

static int test_main(int argc, char **argv)
{
	CHECK(argc == 2);
	long allocations = bnum_allocs();
	char *uuid = os_generate_uuid();
	struct dstr path = {0};
	dstr_printf(&path, "%s/mux-test-%s.tmp", argv[1], uuid);
	bfree(uuid);
	test_unopened_output();
	for (int mode = 0; mode < 4; mode++)
		test_io(path.array, mode);
	dstr_free(&path);
	CHECK(bnum_allocs() == allocations);
	puts("PASS muxer normal completion and write/seek/final-close error propagation");
	return 0;
}

#include "test-utf8-main.h"
