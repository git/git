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

	struct reftable_write_options opts;

	struct reftable_reader **readers;
	size_t readers_len;
	struct reftable_merged_table *merged;
	struct reftable_compaction_stats stats;
};

int read_lines(const char *filename, char ***lines);

struct segment {
	size_t start, end;
	uint64_t bytes;
};

struct segment suggest_compaction_segment(uint64_t *sizes, size_t n,
					  uint8_t factor);

#endif
