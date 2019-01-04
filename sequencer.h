#ifndef SEQUENCER_H
#define SEQUENCER_H

#include "cache.h"
#include "strbuf.h"

struct commit;
struct repository;

const char *git_path_commit_editmsg(void);
const char *git_path_seq_dir(void);
const char *rebase_path_todo(void);

#define APPEND_SIGNOFF_DEDUP (1u << 0)

enum replay_action {
	REPLAY_REVERT,
	REPLAY_PICK,
	REPLAY_INTERACTIVE_REBASE
};

enum commit_msg_cleanup_mode {
	COMMIT_MSG_CLEANUP_SPACE,
	COMMIT_MSG_CLEANUP_NONE,
	COMMIT_MSG_CLEANUP_SCISSORS,
	COMMIT_MSG_CLEANUP_ALL
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
	enum commit_msg_cleanup_mode default_msg_cleanup;

	/* Merge strategy */
	char *strategy;
	char **xopts;
	size_t xopts_nr, xopts_alloc;

	/* Used by fixup/squash */
	struct strbuf current_fixups;
	int current_fixup_count;

	/* placeholder commit for -i --root */
	struct object_id squash_onto;
	int have_squash_onto;

	/* Only used by REPLAY_NONE */
	struct rev_info *revs;
};
#define REPLAY_OPTS_INIT { .action = -1, .current_fixups = STRBUF_INIT }

enum missing_commit_check_level {
	MISSING_COMMIT_CHECK_IGNORE = 0,
	MISSING_COMMIT_CHECK_WARN,
	MISSING_COMMIT_CHECK_ERROR
};

int write_message(const void *buf, size_t len, const char *filename,
		  int append_eol);

/* Call this to setup defaults before parsing command line options */
void sequencer_init_config(struct replay_opts *opts);
int sequencer_pick_revisions(struct repository *repo,
			     struct replay_opts *opts);
int sequencer_continue(struct repository *repo, struct replay_opts *opts);
int sequencer_rollback(struct repository *repo, struct replay_opts *opts);
int sequencer_remove_state(struct replay_opts *opts);

#define TODO_LIST_KEEP_EMPTY (1U << 0)
#define TODO_LIST_SHORTEN_IDS (1U << 1)
#define TODO_LIST_ABBREVIATE_CMDS (1U << 2)
#define TODO_LIST_REBASE_MERGES (1U << 3)
/*
 * When rebasing merges, commits that do have the base commit as ancestor
 * ("cousins") are *not* rebased onto the new base by default. If those
 * commits should be rebased onto the new base, this flag needs to be passed.
 */
#define TODO_LIST_REBASE_COUSINS (1U << 4)
int sequencer_make_script(struct repository *repo, FILE *out,
			  int argc, const char **argv,
			  unsigned flags);

int sequencer_add_exec_commands(struct repository *r, const char *command);
int transform_todos(struct repository *r, unsigned flags);
enum missing_commit_check_level get_missing_commit_check_level(void);
int check_todo_list(struct repository *r);
int complete_action(struct repository *r, struct replay_opts *opts, unsigned flags,
		    const char *shortrevisions, const char *onto_name,
		    const char *onto, const char *orig_head, const char *cmd,
		    unsigned autosquash);
int rearrange_squash(struct repository *r);

extern const char sign_off_header[];

/*
 * Append a signoff to the commit message in "msgbuf". The ignore_footer
 * parameter specifies the number of bytes at the end of msgbuf that should
 * not be considered at all. I.e., they are not checked for existing trailers,
 * and the new signoff will be spliced into the buffer before those bytes.
 */
void append_signoff(struct strbuf *msgbuf, size_t ignore_footer, unsigned flag);

void append_conflicts_hint(struct index_state *istate, struct strbuf *msgbuf);
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

int prepare_branch_to_be_rebased(struct replay_opts *opts, const char *commit);

#define SUMMARY_INITIAL_COMMIT   (1 << 0)
#define SUMMARY_SHOW_AUTHOR_DATE (1 << 1)
void print_commit_summary(struct repository *repo,
			  const char *prefix,
			  const struct object_id *oid,
			  unsigned int flags);

int read_author_script(const char *path, char **name, char **email, char **date,
		       int allow_missing);
#endif

void parse_strategy_opts(struct replay_opts *opts, char *raw_opts);
int write_basic_state(struct replay_opts *opts, const char *head_name,
		      const char *onto, const char *orig_head);
