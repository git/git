#ifndef SEQUENCER_H
#define SEQUENCER_H

const char *git_path_seq_dir(void);

#define APPEND_SIGNOFF_DEDUP (1u << 0)

enum replay_action {
	REPLAY_REVERT,
	REPLAY_PICK,
	REPLAY_INTERACTIVE_REBASE
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
	int verbose;

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

int sequencer_make_script(int keep_empty, FILE *out,
		int argc, const char **argv);

int transform_todo_ids(int shorten_ids);
int check_todo_list(void);
int skip_unnecessary_picks(void);
int rearrange_squash(void);

extern const char sign_off_header[];

void append_signoff(struct strbuf *msgbuf, int ignore_footer, unsigned flag);
void append_conflicts_hint(struct strbuf *msgbuf);
int git_sequencer_config(const char *k, const char *v, void *cb);

enum commit_msg_cleanup_mode {
	COMMIT_MSG_CLEANUP_SPACE,
	COMMIT_MSG_CLEANUP_NONE,
	COMMIT_MSG_CLEANUP_SCISSORS,
	COMMIT_MSG_CLEANUP_ALL
};

int message_is_empty(const struct strbuf *sb,
		     enum commit_msg_cleanup_mode cleanup_mode);
int template_untouched(const struct strbuf *sb, const char *template_file,
		       enum commit_msg_cleanup_mode cleanup_mode);
int update_head_with_reflog(const struct commit *old_head,
			    const struct object_id *new_head,
			    const char *action, const struct strbuf *msg,
			    struct strbuf *err);
void commit_post_rewrite(const struct commit *current_head,
			 const struct object_id *new_head);

#define SUMMARY_INITIAL_COMMIT   (1 << 0)
#define SUMMARY_SHOW_AUTHOR_DATE (1 << 1)
void print_commit_summary(const char *prefix, const struct object_id *oid,
			  unsigned int flags);
#endif
