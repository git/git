#ifndef MAILINFO_H
#define MAILINFO_H

#define MAX_BOUNDARIES 5

struct mailinfo {
	FILE *input;
	FILE *output;
	FILE *patchfile;

	struct strbuf name;
	struct strbuf email;
	int keep_subject;
	int keep_non_patch_brackets_in_subject;
	int add_message_id;
	int use_scissors;
	int use_inbody_headers;
	const char *metainfo_charset;

	struct strbuf *content[MAX_BOUNDARIES];
	struct strbuf **content_top;
	struct strbuf charset;
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

extern void setup_mailinfo(struct mailinfo *);
extern int mailinfo(struct mailinfo *, const char *msg, const char *patch);
extern void clear_mailinfo(struct mailinfo *);

#endif /* MAILINFO_H */
