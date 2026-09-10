#include "replay-disk-storage.h"

#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#include <winioctl.h>
#include <io.h>
#endif

#define REPLAY_DISK_CHUNK_SIZE (16LL * 1024 * 1024)
#define REPLAY_DISK_ALIGNMENT (64LL * 1024)

struct replay_disk_chunk {
	volatile long refs;
	struct replay_disk_file *backing;
	struct replay_disk_chunk *next;
	int64_t offset;
	int64_t capacity;
	int64_t size;
	bool sealed;
};

struct replay_disk_file {
	volatile long refs;
	pthread_mutex_t mutex;
	struct dstr directory;
	struct dstr path;
	struct replay_disk_chunk *free_chunks;
	struct replay_disk_chunk *deferred_chunks;
	int64_t reserved_bytes;
	bool reclaim_paused;
	bool directory_created;
	bool file_created;
	volatile bool sparse;
#ifdef _WIN32
	HANDLE reclaim_handle;
#endif
};

static void replay_disk_file_ref(struct replay_disk_file *file)
{
	os_atomic_inc_long(&file->refs);
}

static void free_chunk_list(struct replay_disk_chunk *chunk)
{
	while (chunk) {
		struct replay_disk_chunk *next = chunk->next;
		bfree(chunk);
		chunk = next;
	}
}

static void replay_disk_file_release(struct replay_disk_file *file)
{
	if (!file || os_atomic_dec_long(&file->refs) != 0) {
		return;
	}
#ifdef _WIN32
	if (file->reclaim_handle != INVALID_HANDLE_VALUE) {
		CloseHandle(file->reclaim_handle);
	}
#endif
	if (file->file_created && os_unlink(file->path.array) != 0) {
		blog(LOG_WARNING, "[replay buffer] Could not remove temporary cache '%s'", file->path.array);
	}
	if (file->directory_created) {
		/* Only our unique session directory, never recursively delete a root. */
		os_rmdir(file->directory.array);
	}
	free_chunk_list(file->free_chunks);
	free_chunk_list(file->deferred_chunks);
	pthread_mutex_destroy(&file->mutex);
	dstr_free(&file->directory);
	dstr_free(&file->path);
	bfree(file);
}

static void reclaim_chunk(struct replay_disk_chunk *chunk)
{
#ifdef _WIN32
	struct replay_disk_file *file = chunk->backing;
	if (os_atomic_load_bool(&file->sparse)) {
		FILE_ZERO_DATA_INFORMATION range = {0};
		range.FileOffset.QuadPart = chunk->offset;
		range.BeyondFinalZero.QuadPart = chunk->offset + chunk->capacity;
		DWORD returned = 0;
		if (!DeviceIoControl(file->reclaim_handle, FSCTL_SET_ZERO_DATA, &range, sizeof(range), NULL, 0,
				     &returned, NULL)) {
			blog(LOG_WARNING,
			     "[replay buffer] Sparse reclamation failed (%lu); freed extents will be reused",
			     GetLastError());
			os_atomic_set_bool(&file->sparse, false);
		}
	}
#else
	UNUSED_PARAMETER(chunk);
#endif
}

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
	struct replay_disk_file *file = chunk->backing;
	pthread_mutex_lock(&file->mutex);
	if (file->reclaim_paused) {
		chunk->next = file->deferred_chunks;
		file->deferred_chunks = chunk;
		pthread_mutex_unlock(&file->mutex);
	} else {
		pthread_mutex_unlock(&file->mutex);
		/* No packet/reader references this extent, and it is not reusable
		 * until reclamation finishes. Do not hold the writer's allocation
		 * lock while the filesystem releases old physical allocation. */
		reclaim_chunk(chunk);
		pthread_mutex_lock(&file->mutex);
		chunk->next = file->free_chunks;
		file->free_chunks = chunk;
		pthread_mutex_unlock(&file->mutex);
	}
	replay_disk_file_release(file);
}

static struct replay_disk_chunk *allocate_chunk(struct replay_disk_store *store, size_t size)
{
	struct replay_disk_file *file = store->backing;
	int64_t capacity = (int64_t)size > store->chunk_limit ? (int64_t)size : store->chunk_limit;
	if (capacity <= 0 || capacity > INT64_MAX - (REPLAY_DISK_ALIGNMENT - 1)) {
		return NULL;
	}
	capacity = (capacity + REPLAY_DISK_ALIGNMENT - 1) & ~(REPLAY_DISK_ALIGNMENT - 1);
	pthread_mutex_lock(&file->mutex);
	struct replay_disk_chunk *chunk = NULL;
	/* While saving, append only. Expired ranges are not recycled until the
	 * muxer has successfully closed the output. */
	if (!file->reclaim_paused) {
		struct replay_disk_chunk **slot = &file->free_chunks;
		while (*slot && (*slot)->capacity < capacity) {
			slot = &(*slot)->next;
		}
		if (*slot) {
			chunk = *slot;
			*slot = chunk->next;
		}
	}
	if (!chunk && file->reserved_bytes <= INT64_MAX - capacity) {
		chunk = bzalloc(sizeof(*chunk));
		chunk->backing = file;
		chunk->offset = file->reserved_bytes;
		chunk->capacity = capacity;
		file->reserved_bytes += capacity;
	}
	if (chunk) {
		chunk->refs = 1;
		chunk->size = 0;
		chunk->sealed = false;
		chunk->next = NULL;
		replay_disk_file_ref(file);
	}
	pthread_mutex_unlock(&file->mutex);
	return chunk;
}

static bool replay_disk_new_chunk(struct replay_disk_store *store, size_t size)
{
	struct replay_disk_chunk *chunk = allocate_chunk(store, size);
	if (!chunk) {
		return false;
	}
	if (os_fseeki64(store->file, chunk->offset, SEEK_SET) != 0) {
		replay_disk_chunk_release(chunk);
		return false;
	}
	store->chunk = chunk;
	return true;
}

bool replay_disk_open(struct replay_disk_store *store, const char *directory)
{
	if (!directory || !*directory || store->file || store->backing || store->chunk) {
		return false;
	}
	struct replay_disk_file *backing = bzalloc(sizeof(*backing));
	if (pthread_mutex_init(&backing->mutex, NULL) != 0) {
		bfree(backing);
		return false;
	}
	backing->refs = 1;
#ifdef _WIN32
	backing->reclaim_handle = INVALID_HANDLE_VALUE;
#endif
	struct dstr root = {0};
	dstr_printf(&root, "%s/OBS-Replay-Cache", directory);
	bool root_ready = os_mkdir(root.array) != MKDIR_ERROR;
	char *uuid = root_ready ? os_generate_uuid() : NULL;
	if (uuid) {
		dstr_printf(&backing->directory, "%s/%s", root.array, uuid);
		bfree(uuid);
		backing->directory_created = os_mkdir(backing->directory.array) == MKDIR_SUCCESS;
	}
	dstr_free(&root);
	if (!backing->directory_created) {
		replay_disk_file_release(backing);
		return false;
	}
	dstr_printf(&backing->path, "%s/cache.tmp", backing->directory.array);
	FILE *file = os_fopen(backing->path.array, "w+bx");
	if (!file) {
		replay_disk_file_release(backing);
		return false;
	}
	backing->file_created = true;
	setvbuf(file, NULL, _IOFBF, 1024 * 1024);
#ifdef _WIN32
	HANDLE handle = (HANDLE)_get_osfhandle(_fileno(file));
	DWORD returned = 0;
	if (DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &backing->reclaim_handle, 0, FALSE,
			    DUPLICATE_SAME_ACCESS)) {
		backing->sparse =
			!!DeviceIoControl(backing->reclaim_handle, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &returned, NULL);
	}
#endif
	if (!backing->sparse) {
		blog(LOG_INFO, "[replay buffer] Single-file cache uses extent reuse (sparse reclamation unavailable)");
	}
	dstr_copy(&store->directory, directory);
	store->chunk_limit = REPLAY_DISK_CHUNK_SIZE;
	store->backing = backing;
	store->file = file;
	return true;
}

bool replay_disk_seal(struct replay_disk_store *store)
{
	if (!store->chunk) {
		return true;
	}
	bool success = fflush(store->file) == 0;
	store->chunk->sealed = success;
	replay_disk_chunk_release(store->chunk);
	store->chunk = NULL;
	return success;
}

void replay_disk_close(struct replay_disk_store *store)
{
	replay_disk_seal(store);
	if (store->file) {
		fclose(store->file);
		store->file = NULL;
	}
	replay_disk_file_release(store->backing);
	store->backing = NULL;
	dstr_free(&store->directory);
}

struct replay_disk_file *replay_disk_begin_save(struct replay_disk_store *store)
{
	struct replay_disk_file *file = store->backing;
	if (!file) {
		return NULL;
	}
	replay_disk_file_ref(file);
	pthread_mutex_lock(&file->mutex);
	file->reclaim_paused = true;
	pthread_mutex_unlock(&file->mutex);
	if (!replay_disk_seal(store)) {
		replay_disk_file_release(file);
		return NULL;
	}
	return file;
}

void replay_disk_end_save(struct replay_disk_file *file, bool success)
{
	if (!file) {
		return;
	}
	if (success) {
		pthread_mutex_lock(&file->mutex);
		struct replay_disk_chunk *chunks = file->deferred_chunks;
		file->deferred_chunks = NULL;
		file->reclaim_paused = false;
		pthread_mutex_unlock(&file->mutex);
		/* A save token pins the file even after stop/restart. Return one
		 * extent at a time so ongoing writes need not wait for a large
		 * post-save reclamation batch under a file-wide lock. */
		while (chunks) {
			struct replay_disk_chunk *next = chunks->next;
			reclaim_chunk(chunks);
			pthread_mutex_lock(&file->mutex);
			chunks->next = file->free_chunks;
			file->free_chunks = chunks;
			pthread_mutex_unlock(&file->mutex);
			chunks = next;
		}
	}
	replay_disk_file_release(file);
}

const char *replay_disk_path(const struct replay_disk_store *store)
{
	return store->backing ? store->backing->path.array : NULL;
}

void replay_disk_get_stats(const struct replay_disk_store *store, struct replay_disk_stats *stats)
{
	memset(stats, 0, sizeof(*stats));
	struct replay_disk_file *file = store->backing;
	if (!file) {
		return;
	}
	pthread_mutex_lock(&file->mutex);
	stats->reserved_bytes = file->reserved_bytes;
	stats->reclaim_paused = file->reclaim_paused;
	stats->sparse = os_atomic_load_bool(&file->sparse);
	for (struct replay_disk_chunk *chunk = file->free_chunks; chunk; chunk = chunk->next) {
		stats->reusable_bytes += chunk->capacity;
	}
	for (struct replay_disk_chunk *chunk = file->deferred_chunks; chunk; chunk = chunk->next) {
		stats->deferred_bytes += chunk->capacity;
	}
	pthread_mutex_unlock(&file->mutex);
}

bool replay_disk_write(struct replay_disk_store *store, const void *data, size_t size, struct replay_disk_chunk **chunk,
		       int64_t *offset)
{
	*chunk = NULL;
	*offset = 0;
	if (!data || !size || size > INT64_MAX || !store->backing || !store->file) {
		return false;
	}
	if (store->chunk && store->chunk->size &&
	    (size > (uint64_t)store->chunk_limit || store->chunk->size > store->chunk_limit - (int64_t)size)) {
		if (!replay_disk_seal(store)) {
			return false;
		}
	}
	if (!store->chunk && !replay_disk_new_chunk(store, size)) {
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
		if (!reader->chunk || reader->chunk->backing != chunk->backing) {
			replay_disk_reader_close(reader);
			reader->file = os_fopen(chunk->backing->path.array, "rb");
			if (!reader->file) {
				return false;
			}
		}
		replay_disk_chunk_ref(chunk);
		replay_disk_chunk_release(reader->chunk);
		reader->chunk = chunk;
	}
	int64_t position = chunk->offset + offset;
	if (reader->position != position && os_fseeki64(reader->file, position, SEEK_SET) != 0) {
		return false;
	}
	if (fread(data, 1, size, reader->file) != size) {
		reader->position = -1;
		return false;
	}
	reader->position = position + (int64_t)size;
	return true;
}
