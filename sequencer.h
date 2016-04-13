#ifndef SEQUENCER_H
#define SEQUENCER_H

const char *git_path_seq_dir(void);

#define APPEND_SIGNOFF_DEDUP (1u << 0)

enum replay_action {
	REPLAY_REVERT,
	REPLAY_PICK
};

struct replay_opts {
	enum replay_action action;

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
#define REPLAY_OPTS_INIT { -1 }

int sequencer_pick_revisions(struct replay_opts *opts);
int sequencer_continue(struct replay_opts *opts);
int sequencer_rollback(struct replay_opts *opts);
int sequencer_remove_state(struct replay_opts *opts);

extern const char sign_off_header[];

void append_signoff(struct strbuf *msgbuf, int ignore_footer, unsigned flag);
void append_conflicts_hint(struct strbuf *msgbuf);

#endif
