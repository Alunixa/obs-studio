#include "replay-disk-storage.h"

#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include <limits.h>

#define REPLAY_DISK_CHUNK_SIZE (16LL * 1024 * 1024)

struct replay_disk_chunk {
	volatile long refs;
	struct dstr path;
	int64_t size;
	bool sealed;
};

void replay_disk_chunk_ref(struct replay_disk_chunk *chunk)
{
	if (chunk) {
		os_atomic_inc_long(&chunk->refs);
	}
}

void replay_disk_chunk_release(struct replay_disk_chunk *chunk)
{
	if (!chunk || os_atomic_dec_long(&chunk->refs) != 0) {
		return;
	}

	if (os_unlink(chunk->path.array) != 0) {
		blog(LOG_WARNING, "[replay buffer] Could not remove temporary chunk '%s'", chunk->path.array);
	}
	dstr_free(&chunk->path);
	bfree(chunk);
}

static bool replay_disk_new_chunk(struct replay_disk_store *store)
{
	struct replay_disk_chunk *chunk = bzalloc(sizeof(*chunk));
	char *uuid = os_generate_uuid();
	if (!uuid) {
		bfree(chunk);
		return false;
	}

	dstr_printf(&chunk->path, "%s/replay-buffer-%s.tmp", store->directory.array, uuid);
	bfree(uuid);

	/* Exclusive creation also protects independent OBS instances. */
	FILE *file = os_fopen(chunk->path.array, "w+bx");
	if (!file) {
		dstr_free(&chunk->path);
		bfree(chunk);
		return false;
	}

	setvbuf(file, NULL, _IOFBF, 1024 * 1024);
	chunk->refs = 1;
	store->chunk = chunk;
	store->file = file;
	return true;
}

bool replay_disk_open(struct replay_disk_store *store, const char *directory)
{
	if (!directory || !*directory || store->file || store->chunk) {
		return false;
	}

	dstr_copy(&store->directory, directory);
	store->chunk_limit = REPLAY_DISK_CHUNK_SIZE;
	if (!replay_disk_new_chunk(store)) {
		dstr_free(&store->directory);
		return false;
	}
	return true;
}

bool replay_disk_seal(struct replay_disk_store *store)
{
	if (!store->file) {
		return true;
	}

	bool success = fflush(store->file) == 0;
	if (fclose(store->file) != 0) {
		success = false;
	}
	store->file = NULL;
	store->chunk->sealed = success;
	replay_disk_chunk_release(store->chunk);
	store->chunk = NULL;
	return success;
}

void replay_disk_close(struct replay_disk_store *store)
{
	replay_disk_seal(store);
	dstr_free(&store->directory);
}

bool replay_disk_write(struct replay_disk_store *store, const void *data, size_t size, struct replay_disk_chunk **chunk,
		       int64_t *offset)
{
	*chunk = NULL;
	*offset = 0;
	if (!data || !size || size > INT64_MAX || !store->directory.len) {
		return false;
	}

	/* Never overwrite a chunk. Retention is controlled by packet references,
	 * so a slow save cannot race a wraparound or stop/restart of the writer. */
	if (store->chunk && store->chunk->size &&
	    (size > (uint64_t)store->chunk_limit || store->chunk->size > store->chunk_limit - (int64_t)size)) {
		if (!replay_disk_seal(store)) {
			return false;
		}
	}
	if (!store->file && !replay_disk_new_chunk(store)) {
		return false;
	}
	if (fwrite(data, 1, size, store->file) != size) {
		return false;
	}

	*chunk = store->chunk;
	*offset = store->chunk->size;
	store->chunk->size += (int64_t)size;
	replay_disk_chunk_ref(*chunk);
	return true;
}

void replay_disk_reader_close(struct replay_disk_reader *reader)
{
	if (reader->file) {
		fclose(reader->file);
	}
	reader->file = NULL;
	replay_disk_chunk_release(reader->chunk);
	reader->chunk = NULL;
	reader->position = 0;
}

bool replay_disk_read(struct replay_disk_reader *reader, struct replay_disk_chunk *chunk, int64_t offset, void *data,
		      size_t size)
{
	if (!chunk || !chunk->sealed || !data || offset < 0 || offset > chunk->size ||
	    size > (uint64_t)(chunk->size - offset)) {
		return false;
	}

	if (reader->chunk != chunk) {
		replay_disk_reader_close(reader);
		reader->file = os_fopen(chunk->path.array, "rb");
		if (!reader->file) {
			return false;
		}
		replay_disk_chunk_ref(chunk);
		reader->chunk = chunk;
	}
	if (reader->position != offset && os_fseeki64(reader->file, offset, SEEK_SET) != 0) {
		return false;
	}
	if (fread(data, 1, size, reader->file) != size) {
		reader->position = -1;
		return false;
	}
	reader->position = offset + (int64_t)size;
	return true;
}
