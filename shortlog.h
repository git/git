#ifndef SHORTLOG_H
#define SHORTLOG_H

#include "path-list.h"

struct shortlog {
	struct path_list list;
	int summary;
	int wrap_lines;
	int sort_by_number;
	int wrap;
	int in1;
	int in2;

	char *common_repo_prefix;
	int email;
	struct path_list mailmap;
};

void shortlog_init(struct shortlog *log);

void shortlog_add_commit(struct shortlog *log, struct commit *commit);

void shortlog_output(struct shortlog *log);

#endif
