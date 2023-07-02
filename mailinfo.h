#ifndef MAILINFO_H
#define MAILINFO_H

#include "strbuf.h"

#define MAX_BOUNDARIES 5

enum quoted_cr_action {
	quoted_cr_unset = -1,
	quoted_cr_nowarn,
	quoted_cr_warn,
	quoted_cr_strip,
};

struct mailinfo {
	FILE *input;
	FILE *output;
	FILE *patchfile;

	struct strbuf name;
	struct strbuf email;
	int keep_subject;
	int keep_non_patch_brackets_in_subject;
	int quoted_cr; /* enum quoted_cr_action */
	int add_message_id;
	int use_scissors;
	int use_inbody_headers;
	const char *metainfo_charset;

	struct strbuf *content[MAX_BOUNDARIES];
	struct strbuf **content_top;
	struct strbuf charset;
	unsigned int format_flowed:1;
	unsigned int delsp:1;
	unsigned int have_quoted_cr:1;
	char *message_id;
	enum  {
		TE_DONTCARE, TE_QP, TE_BASE64
	} transfer_encoding;
	int patch_lines;
	int filter_stage; /* still reading log or are we copying patch? */
	int header_stage; /* still checking in-body headers? */
	struct strbuf inbody_header_accum;
	struct strbuf **p_hdr_data;
	struct strbuf **s_hdr_data;

	struct strbuf log_message;
	int input_error;
};

int mailinfo_parse_quoted_cr_action(const char *actionstr, int *action);
void setup_mailinfo(struct mailinfo *);
int mailinfo(struct mailinfo *, const char *msg, const char *patch);
void clear_mailinfo(struct mailinfo *);

#endif /* MAILINFO_H */
