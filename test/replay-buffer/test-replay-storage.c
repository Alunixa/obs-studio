#include "replay-disk-storage.h"
#include "nvenc-upload.h"

#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/pipe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

#define CHECK(condition)                                                                 \
	do {                                                                             \
		if (!(condition)) {                                                       \
			fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			exit(1);                                                         \
		}                                                                        \
	} while (0)

struct saved_packet {
	struct replay_disk_chunk *chunk;
	int64_t offset;
	uint8_t value;
};

static size_t chunk_count(const char *directory)
{
	struct dstr pattern = {0};
	os_glob_t *files = NULL;
	dstr_printf(&pattern, "%s/OBS-Replay-Cache/*", directory);
	os_glob(pattern.array, 0, &files);
	size_t count = 0;
	for (size_t i = 0; files && i < files->gl_pathc; i++) {
		dstr_printf(&pattern, "%s/cache.tmp", files->gl_pathv[i].path);
		count += os_file_exists(pattern.array) ? 1 : 0;
	}
	os_globfree(files);
	dstr_free(&pattern);
	return count;
}

static void test_lifetime(const char *directory)
{
	struct replay_disk_store store = {0};
	struct replay_disk_reader reader = {0};
	struct replay_disk_chunk *chunk = NULL;
	int64_t offset = 0;
	uint8_t data[513];
	uint8_t result[513];
	memset(data, 0xa7, sizeof(data));

	CHECK(!replay_disk_open(&store, ""));
	CHECK(replay_disk_open(&store, directory));
	CHECK(!replay_disk_write(&store, NULL, sizeof(data), &chunk, &offset));
	CHECK(replay_disk_write(&store, data, sizeof(data), &chunk, &offset));
	CHECK(!replay_disk_read(&reader, chunk, offset, result, sizeof(result)));
	struct replay_disk_file *save = replay_disk_begin_save(&store);
	CHECK(save != NULL);
	CHECK(!replay_disk_read(&reader, chunk, -1, result, sizeof(result)));
	CHECK(!replay_disk_read(&reader, chunk, offset + 1, result, sizeof(result)));

	/* Snapshot holds its own reference after retention and stop release theirs. */
	replay_disk_chunk_ref(chunk);
	replay_disk_chunk_release(chunk);
	replay_disk_close(&store);
	CHECK(chunk_count(directory) == 1);
	CHECK(replay_disk_open(&store, directory));
	CHECK(replay_disk_read(&reader, chunk, offset, result, sizeof(result)));
	CHECK(memcmp(data, result, sizeof(data)) == 0);
	replay_disk_chunk_release(chunk);
	CHECK(chunk_count(directory) == 2); /* Reader pins the old chunk, too. */
	replay_disk_reader_close(&reader);
	replay_disk_close(&store);
	replay_disk_end_save(save, true);
	CHECK(chunk_count(directory) == 0);
	puts("PASS immutable snapshot survives stop/restart; reader pins file; cleanup");
}

static void *slow_reader(void *data)
{
	struct saved_packet *packets = data;
	struct replay_disk_reader reader = {0};
	uint8_t result[4096];
	for (size_t i = 0; i < 64; i++) {
		CHECK(replay_disk_read(&reader, packets[i].chunk, packets[i].offset, result, sizeof(result)));
		for (size_t byte = 0; byte < sizeof(result); byte++) {
			CHECK(result[byte] == packets[i].value);
		}
		replay_disk_chunk_release(packets[i].chunk);
		os_sleep_ms(1);
	}
	replay_disk_reader_close(&reader);
	return NULL;
}

static void test_concurrent_save(const char *directory)
{
	struct replay_disk_store store = {0};
	struct saved_packet packets[64] = {0};
	uint8_t data[4096];
	CHECK(replay_disk_open(&store, directory));
	store.chunk_limit = 16384;
	for (size_t i = 0; i < 64; i++) {
		memset(data, (uint8_t)i, sizeof(data));
		packets[i].value = (uint8_t)i;
		CHECK(replay_disk_write(&store, data, sizeof(data), &packets[i].chunk, &packets[i].offset));
	}
	struct replay_disk_file *save = replay_disk_begin_save(&store);
	CHECK(save != NULL);
	pthread_t thread;
	CHECK(pthread_create(&thread, NULL, slow_reader, packets) == 0);

	for (size_t i = 0; i < 2048; i++) {
		struct replay_disk_chunk *chunk = NULL;
		int64_t offset = 0;
		memset(data, (uint8_t)(i + 91), sizeof(data));
		CHECK(replay_disk_write(&store, data, sizeof(data), &chunk, &offset));
		replay_disk_chunk_release(chunk);
	}
	replay_disk_close(&store);
	CHECK(pthread_join(thread, NULL) == 0);
	CHECK(chunk_count(directory) == 1);
	replay_disk_end_save(save, true);
	CHECK(chunk_count(directory) == 0);
	puts("PASS one cache file; slow save concurrent with 2048 appends and stop");
}

static uint8_t first_byte(const char *path)
{
	FILE *file = os_fopen(path, "rb");
	CHECK(file != NULL);
	int byte = fgetc(file);
	CHECK(byte != EOF);
	CHECK(fclose(file) == 0);
	return (uint8_t)byte;
}

#ifdef _WIN32
static uint64_t allocated_size(const struct replay_disk_store *store)
{
	/* Account for physical allocation, not pending cached writes. */
	CHECK(fflush(store->file) == 0);
	CHECK(FlushFileBuffers((HANDLE)_get_osfhandle(_fileno(store->file))));
	wchar_t *wide = NULL;
	CHECK(os_utf8_to_wcs_ptr(replay_disk_path(store), 0, &wide) != 0);
	DWORD high = 0;
	SetLastError(NO_ERROR);
	DWORD low = GetCompressedFileSizeW(wide, &high);
	CHECK(low != INVALID_FILE_SIZE || GetLastError() == NO_ERROR);
	bfree(wide);
	return ((uint64_t)high << 32) | low;
}
#endif

static void test_deferred_reclamation(const char *directory)
{
	struct replay_disk_store store = {0};
	struct replay_disk_reader reader = {0};
	struct replay_disk_stats before, failed, after;
	struct replay_disk_chunk *old = NULL, *retained = NULL, *extra = NULL;
	int64_t old_offset, retained_offset, extra_offset;
	uint8_t *data = bmalloc(65536);
	uint8_t *result = bmalloc(65536);
	CHECK(replay_disk_open(&store, directory));
	store.chunk_limit = 65536;
	memset(data, 0x41, 65536);
	CHECK(replay_disk_write(&store, data, 65536, &old, &old_offset));
	struct replay_disk_file *save = replay_disk_begin_save(&store);
	CHECK(save != NULL);
	memset(data, 0x42, 65536);
	CHECK(replay_disk_write(&store, data, 65536, &retained, &retained_offset));
	CHECK(replay_disk_seal(&store));
	replay_disk_chunk_release(old);
	replay_disk_get_stats(&store, &before);
	CHECK(before.reclaim_paused && before.deferred_bytes == 65536 && before.reusable_bytes == 0);
	CHECK(first_byte(replay_disk_path(&store)) == 0x41);
	CHECK(chunk_count(directory) == 1);

	replay_disk_end_save(save, false);
	replay_disk_get_stats(&store, &failed);
	CHECK(failed.reclaim_paused && failed.deferred_bytes == before.deferred_bytes);
	CHECK(first_byte(replay_disk_path(&store)) == 0x41);
	memset(data, 0x43, 65536);
	CHECK(replay_disk_write(&store, data, 65536, &extra, &extra_offset));
	CHECK(replay_disk_seal(&store));
	replay_disk_chunk_release(extra);
	replay_disk_get_stats(&store, &failed);
	CHECK(failed.reserved_bytes == 3 * 65536 && failed.deferred_bytes == 2 * 65536);
	CHECK(first_byte(replay_disk_path(&store)) == 0x41);
#ifdef _WIN32
	uint64_t allocated_before = allocated_size(&store);
#endif

	save = replay_disk_begin_save(&store);
	CHECK(save != NULL);
	replay_disk_end_save(save, true);
	replay_disk_get_stats(&store, &after);
	CHECK(!after.reclaim_paused && after.deferred_bytes == 0 && after.reusable_bytes == 2 * 65536);
	CHECK(replay_disk_read(&reader, retained, retained_offset, result, 65536));
	for (size_t i = 0; i < 65536; i++) {
		CHECK(result[i] == 0x42);
	}
#ifdef _WIN32
	if (after.sparse) {
		CHECK(first_byte(replay_disk_path(&store)) == 0);
		uint64_t allocated_after = allocated_size(&store);
		CHECK(allocated_after > 0 && allocated_after < allocated_before);
		printf("PASS sparse allocation reclaimed: %llu -> %llu bytes\n", (unsigned long long)allocated_before,
		       (unsigned long long)allocated_after);
	}
#endif
	for (int i = 0; i < 128; i++) {
		CHECK(replay_disk_write(&store, data, 65536, &extra, &extra_offset));
		CHECK(replay_disk_seal(&store));
		replay_disk_chunk_release(extra);
	}
	replay_disk_get_stats(&store, &after);
	CHECK(after.reserved_bytes == failed.reserved_bytes);
	CHECK(chunk_count(directory) == 1);
	CHECK(replay_disk_read(&reader, retained, retained_offset, result, 65536));
	CHECK(result[0] == 0x42 && result[65535] == 0x42);
	replay_disk_chunk_release(retained);
	replay_disk_reader_close(&reader);
	replay_disk_close(&store);
	bfree(result);
	bfree(data);
	CHECK(chunk_count(directory) == 0);
	puts("PASS failed save defers reclamation; successful retry frees only expired extents; bounded reuse");
}

static void test_failures(const char *directory)
{
	struct replay_disk_store store = {0};
	struct replay_disk_reader reader = {0};
	struct replay_disk_chunk *chunk = NULL;
	int64_t offset = 0;
	uint8_t data[32] = {1};
	uint8_t result[32];
	struct dstr pattern = {0};

	dstr_printf(&pattern, "%s/nonexistent/child", directory);
	CHECK(!replay_disk_open(&store, pattern.array));
	CHECK(replay_disk_open(&store, directory));
	store.chunk_limit = 1;
	CHECK(replay_disk_write(&store, data, sizeof(data), &chunk, &offset));
	CHECK(replay_disk_seal(&store)); /* Packet larger than a chunk is valid. */
	FILE *file = os_fopen(replay_disk_path(&store), "wb");
	CHECK(file != NULL);
	CHECK(fclose(file) == 0); /* Simulate externally truncated/corrupt storage. */
	CHECK(!replay_disk_read(&reader, chunk, offset, result, sizeof(result)));
	replay_disk_reader_close(&reader);
	replay_disk_chunk_release(chunk);
	replay_disk_close(&store);
	dstr_free(&pattern);
	CHECK(chunk_count(directory) == 0);

	/* A read-only stream deterministically injects a write failure. */
	CHECK(replay_disk_open(&store, directory));
	CHECK(fclose(store.file) == 0);
	store.file = os_fopen(replay_disk_path(&store), "rb");
	CHECK(store.file != NULL);
	CHECK(!replay_disk_write(&store, data, sizeof(data), &chunk, &offset));
	CHECK(chunk == NULL);
	replay_disk_close(&store);
	dstr_free(&pattern);
	CHECK(chunk_count(directory) == 0);
	puts("PASS invalid directory, oversized packet, short read and write failure");
}

static void test_instances(const char *directory)
{
	for (int i = 0; i < 50; i++) {
		struct replay_disk_store a = {0};
		struct replay_disk_store b = {0};
		CHECK(replay_disk_open(&a, directory));
		CHECK(replay_disk_open(&b, directory));
		CHECK(chunk_count(directory) == 2);
		replay_disk_close(&a);
		replay_disk_close(&b);
	}
	CHECK(chunk_count(directory) == 0);
	puts("PASS 50 dual-instance cycles without filename collision or temp leaks");
}

static void test_frame_layout(void)
{
	const enum video_format formats[] = {VIDEO_FORMAT_NV12, VIDEO_FORMAT_P010, VIDEO_FORMAT_I444};
	for (size_t f = 0; f < 3; f++) {
		uint8_t planes[3][128];
		uint8_t dst[128];
		struct encoder_frame frame = {0};
		enum video_format format = formats[f];
		size_t pitch = format == VIDEO_FORMAT_P010 ? 8 : 4;
		size_t count = format == VIDEO_FORMAT_I444 ? 3 : 2;
		for (size_t p = 0; p < count; p++) {
			memset(planes[p], 0xee, sizeof(planes[p]));
			frame.data[p] = planes[p];
			frame.linesize[p] = (uint32_t)pitch + 8;
			size_t rows = p && format != VIDEO_FORMAT_I444 ? 2 : 4;
			for (size_t r = 0; r < rows; r++) {
				memset(frame.data[p] + r * frame.linesize[p], (int)(p * 16 + r), pitch);
			}
		}
		size_t size = nvenc_upload_size(4, 4, format);
		CHECK(nvenc_pack_frame(dst, sizeof(dst), &frame, 4, 4, format));
		CHECK(!nvenc_pack_frame(dst, size - 1, &frame, 4, 4, format));
		size_t offset = 0;
		for (size_t p = 0; p < count; p++) {
			size_t rows = p && format != VIDEO_FORMAT_I444 ? 2 : 4;
			for (size_t r = 0; r < rows; r++) {
				for (size_t byte = 0; byte < pitch; byte++) {
					CHECK(dst[offset++] == (uint8_t)(p * 16 + r));
				}
			}
		}
		CHECK(offset == size);
		frame.linesize[0] = 1;
		CHECK(!nvenc_pack_frame(dst, sizeof(dst), &frame, 4, 4, format));
		frame.linesize[0] = 32;
		frame.data[1] = NULL;
		CHECK(!nvenc_pack_frame(dst, sizeof(dst), &frame, 4, 4, format));
	}
	CHECK(nvenc_upload_size(0, 4, VIDEO_FORMAT_NV12) == 0);
	CHECK(nvenc_upload_size(3, 4, VIDEO_FORMAT_NV12) == 0);
	CHECK(nvenc_upload_size(4, 4, VIDEO_FORMAT_NONE) == 0);
	CHECK(nvenc_upload_size(UINT32_MAX, UINT32_MAX, VIDEO_FORMAT_I444) == 0);
	puts("PASS NV12/P010/I444 plane contents, strides, bounds and overflow");
}

#ifdef _WIN32
static void test_pipe_timeout(const char *executable)
{
	for (int mode = 0; mode < 4; mode++) {
		os_process_args_t *args = os_process_args_create(executable);
		const char *modes[] = {"--hang", "--write-then-hang", "--exit", "--write-exit"};
		os_process_args_add_arg(args, modes[mode]);
		uint64_t started = os_gettime_ns();
		os_process_pipe_t *pipe = os_process_pipe_create2(args, "r");
		os_process_args_destroy(args);
		CHECK(pipe != NULL);
		os_process_pipe_set_read_timeout(pipe, 200);
		uint8_t bytes[64];
		size_t total = 0;
		size_t count = 0;
		while ((count = os_process_pipe_read(pipe, bytes, sizeof(bytes))) != 0) {
			total += count;
		}
		int code = os_process_pipe_destroy(pipe);
		CHECK(code == (mode >= 2 ? 0 : ERROR_TIMEOUT));
		CHECK(total == (mode == 1 || mode == 3 ? 5 : 0));
		CHECK(os_gettime_ns() - started < 3000000000ULL);
	}
	puts("PASS parent timeout for silent/partial-output probes and normal exit");
}
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
	if (argc == 2 && strcmp(argv[1], "--exit") == 0) {
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--write-exit") == 0) {
		fwrite("hello", 1, 5, stdout);
		return 0;
	}
	if (argc == 2 && (strcmp(argv[1], "--hang") == 0 || strcmp(argv[1], "--write-then-hang") == 0)) {
		if (strcmp(argv[1], "--write-then-hang") == 0) {
			fwrite("hello", 1, 5, stdout);
			fflush(stdout);
		}
		Sleep(10000);
		return 0;
	}
#endif
	CHECK(argc == 2);
	long allocations = bnum_allocs();
	char *uuid = os_generate_uuid();
	struct dstr directory = {0};
	dstr_printf(&directory, "%s/replay-tests-%s", argv[1], uuid);
	bfree(uuid);
	CHECK(os_mkdirs(directory.array) == 0);
	test_lifetime(directory.array);
	test_concurrent_save(directory.array);
	test_deferred_reclamation(directory.array);
	test_failures(directory.array);
	test_instances(directory.array);
	test_frame_layout();
#ifdef _WIN32
	test_pipe_timeout(argv[0]);
#endif
	struct dstr cache_root = {0};
	dstr_printf(&cache_root, "%s/OBS-Replay-Cache", directory.array);
	CHECK(os_rmdir(cache_root.array) == 0);
	dstr_free(&cache_root);
	CHECK(os_rmdir(directory.array) == 0);
	dstr_free(&directory);
	CHECK(bnum_allocs() == allocations);
	puts("ALL REPLAY REGRESSION TESTS PASSED");
	return 0;
}
