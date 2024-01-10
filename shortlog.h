#ifndef SHORTLOG_H
#define SHORTLOG_H

#include "string-list.h"
#include "date.h"

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
	struct date_mode date_mode;

	enum {
		SHORTLOG_GROUP_AUTHOR = (1 << 0),
		SHORTLOG_GROUP_COMMITTER = (1 << 1),
		SHORTLOG_GROUP_TRAILER = (1 << 2),
		SHORTLOG_GROUP_FORMAT = (1 << 3),
	} groups;
	struct string_list trailers;
	struct string_list format;

	int email;
	struct string_list mailmap;
	FILE *file;
};

void shortlog_init(struct shortlog *log);
void shortlog_finish_setup(struct shortlog *log);

void shortlog_add_commit(struct shortlog *log, struct commit *commit);

void shortlog_output(struct shortlog *log);

#endif
