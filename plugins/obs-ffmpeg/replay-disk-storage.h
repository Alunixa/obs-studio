#pragma once

#include <util/dstr.h>
#include <stdio.h>

/* Sealed chunks are immutable. Packets and readers keep them alive independently
 * of the recording session, including while a replay is being saved. */
struct replay_disk_chunk;

struct replay_disk_store {
	struct dstr directory;
	FILE *file;
	struct replay_disk_chunk *chunk;
	int64_t chunk_limit;
};

struct replay_disk_reader {
	FILE *file;
	struct replay_disk_chunk *chunk;
	int64_t position;
};

bool replay_disk_open(struct replay_disk_store *store, const char *directory);
bool replay_disk_seal(struct replay_disk_store *store);
void replay_disk_close(struct replay_disk_store *store);
bool replay_disk_write(struct replay_disk_store *store, const void *data, size_t size, struct replay_disk_chunk **chunk,
		       int64_t *offset);
void replay_disk_chunk_ref(struct replay_disk_chunk *chunk);
void replay_disk_chunk_release(struct replay_disk_chunk *chunk);
bool replay_disk_read(struct replay_disk_reader *reader, struct replay_disk_chunk *chunk, int64_t offset, void *data,
		      size_t size);
void replay_disk_reader_close(struct replay_disk_reader *reader);
