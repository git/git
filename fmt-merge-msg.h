#ifndef FMT_MERGE_MSG_H
#define FMT_MERGE_MSG_H

#include "strbuf.h"

#define DEFAULT_MERGE_LOG_LEN 20

struct fmt_merge_msg_opts {
	unsigned add_title:1,
		credit_people:1;
	int shortlog_len;
};

extern int merge_log_config;
int fmt_merge_msg_config(const char *key, const char *value, void *cb);
int fmt_merge_msg(struct strbuf *in, struct strbuf *out,
		  struct fmt_merge_msg_opts *);


#endif /* FMT_MERGE_MSG_H */
