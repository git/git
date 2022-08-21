#ifndef SHORTLOG_H
#define SHORTLOG_H

#include "string-list.h"

struct commit;

struct shortlog {
	struct string_list list;
	int summary;
	int wrap_lines;
	int sort_by_number;
	int wrap;
	int in1;
	int in2;
	int user_format;
	int abbrev;

	enum {
		SHORTLOG_GROUP_AUTHOR = (1 << 0),
		SHORTLOG_GROUP_COMMITTER = (1 << 1),
		SHORTLOG_GROUP_TRAILER = (1 << 2),
	} groups;
	struct string_list trailers;

	int email;
	struct string_list mailmap;
	FILE *file;
};

void shortlog_init(struct shortlog *log);

void shortlog_add_commit(struct shortlog *log, struct commit *commit);

void shortlog_output(struct shortlog *log);

#endif
