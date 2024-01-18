/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef STACK_H
#define STACK_H

#include "system.h"
#include "reftable-writer.h"
#include "reftable-stack.h"

struct reftable_stack {
	struct stat list_st;
	char *list_file;
	int list_fd;

	char *reftable_dir;
	int disable_auto_compact;

	struct reftable_write_options config;

	struct reftable_reader **readers;
	size_t readers_len;
	struct reftable_merged_table *merged;
	struct reftable_compaction_stats stats;
};

int read_lines(const char *filename, char ***lines);

struct segment {
	int start, end;
	int log;
	uint64_t bytes;
};

int fastlog2(uint64_t sz);
struct segment *sizes_to_segments(int *seglen, uint64_t *sizes, int n);
struct segment suggest_compaction_segment(uint64_t *sizes, int n);

#endif
