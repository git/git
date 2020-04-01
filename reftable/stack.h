/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#ifndef STACK_H
#define STACK_H

#include "reftable.h"

struct reftable_stack {
	char *list_file;
	char *reftable_dir;

	struct reftable_write_options config;

	struct reftable_merged_table *merged;
	struct reftable_compaction_stats stats;
};

int read_lines(const char *filename, char ***lines);
int stack_try_add(struct reftable_stack *st,
		  int (*write_table)(struct reftable_writer *wr, void *arg),
		  void *arg);
int stack_write_compact(struct reftable_stack *st, struct reftable_writer *wr,
			int first, int last,
			struct reftable_log_expiry_config *config);
int fastlog2(uint64_t sz);

struct segment {
	int start, end;
	int log;
	uint64_t bytes;
};

struct segment *sizes_to_segments(int *seglen, uint64_t *sizes, int n);
struct segment suggest_compaction_segment(uint64_t *sizes, int n);

#endif
