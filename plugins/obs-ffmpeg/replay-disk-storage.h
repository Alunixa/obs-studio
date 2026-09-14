#pragma once

#include <util/dstr.h>
#include <stdio.h>

/* Chunks are immutable extents inside ONE backing file, not separate files.
 * Packets/readers pin extents; a save token also pauses physical reclamation. */
struct replay_disk_chunk;
struct replay_disk_file;

struct replay_disk_options {
	int64_t max_bytes;
	uint64_t min_free_bytes;
	bool disable_sparse;
};

struct replay_disk_store {
	struct dstr directory;
	FILE *file;
	struct replay_disk_file *backing;
	struct replay_disk_chunk *chunk;
	int64_t chunk_limit;
	int64_t max_bytes;
	uint64_t min_free_bytes;
};

struct replay_disk_reader {
	FILE *file;
	struct replay_disk_chunk *chunk;
	int64_t position;
};

struct replay_disk_stats {
	int64_t reserved_bytes;
	int64_t reusable_bytes;
	int64_t deferred_bytes;
	bool reclaim_paused;
	bool sparse;
};

bool replay_disk_open(struct replay_disk_store *store, const char *directory,
		      const struct replay_disk_options *options);
bool replay_disk_seal(struct replay_disk_store *store);
void replay_disk_close(struct replay_disk_store *store);
/* The token survives stop/restart. Both outcomes resume reclamation; callers
 * must keep packet references for any failed snapshot they wish to preserve. */
struct replay_disk_file *replay_disk_begin_save(struct replay_disk_store *store);
void replay_disk_end_save(struct replay_disk_file *token, bool success);
const char *replay_disk_path(const struct replay_disk_store *store);
void replay_disk_get_stats(const struct replay_disk_store *store, struct replay_disk_stats *stats);
bool replay_disk_write(struct replay_disk_store *store, const void *data, size_t size, struct replay_disk_chunk **chunk,
		       int64_t *offset);
void replay_disk_chunk_ref(struct replay_disk_chunk *chunk);
void replay_disk_chunk_release(struct replay_disk_chunk *chunk);
bool replay_disk_read(struct replay_disk_reader *reader, struct replay_disk_chunk *chunk, int64_t offset, void *data,
		      size_t size);
void replay_disk_reader_close(struct replay_disk_reader *reader);
