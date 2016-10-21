#ifndef SEQUENCER_H
#define SEQUENCER_H

const char *git_path_seq_dir(void);

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

	char *gpg_sign;

	/* Merge strategy */
	char *strategy;
	char **xopts;
	size_t xopts_nr, xopts_alloc;

	/* Only used by REPLAY_NONE */
	struct rev_info *revs;
};
#define REPLAY_OPTS_INIT { -1, -1 }

int sequencer_pick_revisions(struct replay_opts *opts);

extern const char sign_off_header[];

void append_signoff(struct strbuf *msgbuf, int ignore_footer, unsigned flag);
void append_conflicts_hint(struct strbuf *msgbuf);

#endif
