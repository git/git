#ifndef SEQUENCER_H
#define SEQUENCER_H

#define SEQ_DIR		"sequencer"
#define SEQ_HEAD_FILE	"sequencer/head"
#define SEQ_TODO_FILE	"sequencer/todo"
#define SEQ_OPTS_FILE	"sequencer/opts"

#define APPEND_SIGNOFF_DEDUP (1u << 0)

enum replay_action {
	REPLAY_REVERT,
	REPLAY_PICK
};

enum replay_subcommand {
	REPLAY_NONE,
	REPLAY_REMOVE_STATE,
	REPLAY_CONTINUE,
	REPLAY_ROLLBACK
};

struct replay_opts {
	enum replay_action action;
	enum replay_subcommand subcommand;

	/* Boolean options */
	int edit;
	int record_origin;
	int no_commit;
	int signoff;
	int allow_ff;
	int allow_rerere_auto;
	int allow_empty;
	int allow_empty_message;
	int keep_redundant_commits;

	int mainline;

	const char *gpg_sign;

	/* Merge strategy */
	const char *strategy;
	const char **xopts;
	size_t xopts_nr, xopts_alloc;

	/* Only used by REPLAY_NONE */
	struct rev_info *revs;
};

int sequencer_pick_revisions(struct replay_opts *opts);

extern const char sign_off_header[];

void append_signoff(struct strbuf *msgbuf, int ignore_footer, unsigned flag);

#endif
