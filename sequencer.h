#ifndef SEQUENCER_H
#define SEQUENCER_H

#include "cache.h"
#include "strbuf.h"
#include "wt-status.h"

struct commit;
struct repository;

const char *git_path_commit_editmsg(void);
const char *rebase_path_todo(void);
const char *rebase_path_todo_backup(void);
const char *rebase_path_dropped(void);

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
	int drop_redundant_commits;
	int keep_redundant_commits;
	int verbose;
	int quiet;
	int reschedule_failed_exec;

	int mainline;

	char *gpg_sign;
	enum commit_msg_cleanup_mode default_msg_cleanup;
	int explicit_cleanup;

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

/*
 * Note that ordering matters in this enum. Not only must it match the mapping
 * of todo_command_info (in sequencer.c), it is also divided into several
 * sections that matter.  When adding new commands, make sure you add it in the
 * right section.
 */
enum todo_command {
	/* commands that handle commits */
	TODO_PICK = 0,
	TODO_REVERT,
	TODO_EDIT,
	TODO_REWORD,
	TODO_FIXUP,
	TODO_SQUASH,
	/* commands that do something else than handling a single commit */
	TODO_EXEC,
	TODO_BREAK,
	TODO_LABEL,
	TODO_RESET,
	TODO_MERGE,
	/* commands that do nothing but are counted for reporting progress */
	TODO_NOOP,
	TODO_DROP,
	/* comments (not counted for reporting progress) */
	TODO_COMMENT
};

struct todo_item {
	enum todo_command command;
	struct commit *commit;
	unsigned int flags;
	int arg_len;
	/* The offset of the command and its argument in the strbuf */
	size_t offset_in_buf, arg_offset;
};

struct todo_list {
	struct strbuf buf;
	struct todo_item *items;
	int nr, alloc, current;
	int done_nr, total_nr;
	struct stat_data stat;
};

#define TODO_LIST_INIT { STRBUF_INIT }

int todo_list_parse_insn_buffer(struct repository *r, char *buf,
				struct todo_list *todo_list);
int todo_list_write_to_file(struct repository *r, struct todo_list *todo_list,
			    const char *file, const char *shortrevisions,
			    const char *shortonto, int num, unsigned flags);
void todo_list_release(struct todo_list *todo_list);
const char *todo_item_get_arg(struct todo_list *todo_list,
			      struct todo_item *item);

/* Call this to setup defaults before parsing command line options */
void sequencer_init_config(struct replay_opts *opts);
int sequencer_pick_revisions(struct repository *repo,
			     struct replay_opts *opts);
int sequencer_continue(struct repository *repo, struct replay_opts *opts);
int sequencer_rollback(struct repository *repo, struct replay_opts *opts);
int sequencer_skip(struct repository *repo, struct replay_opts *opts);
int sequencer_remove_state(struct replay_opts *opts);

/* #define TODO_LIST_KEEP_EMPTY (1U << 0) */ /* No longer used */
#define TODO_LIST_SHORTEN_IDS (1U << 1)
#define TODO_LIST_ABBREVIATE_CMDS (1U << 2)
#define TODO_LIST_REBASE_MERGES (1U << 3)
/*
 * When rebasing merges, commits that do have the base commit as ancestor
 * ("cousins") are *not* rebased onto the new base by default. If those
 * commits should be rebased onto the new base, this flag needs to be passed.
 */
#define TODO_LIST_REBASE_COUSINS (1U << 4)
#define TODO_LIST_APPEND_TODO_HELP (1U << 5)
/*
 * When generating a script that rebases merges with `--root` *and* with
 * `--onto`, we do not want to re-generate the root commits.
 */
#define TODO_LIST_ROOT_WITH_ONTO (1U << 6)


int sequencer_make_script(struct repository *r, struct strbuf *out, int argc,
			  const char **argv, unsigned flags);

void todo_list_add_exec_commands(struct todo_list *todo_list,
				 struct string_list *commands);
int complete_action(struct repository *r, struct replay_opts *opts, unsigned flags,
		    const char *shortrevisions, const char *onto_name,
		    struct commit *onto, const char *orig_head, struct string_list *commands,
		    unsigned autosquash, struct todo_list *todo_list);
int todo_list_rearrange_squash(struct todo_list *todo_list);

/*
 * Append a signoff to the commit message in "msgbuf". The ignore_footer
 * parameter specifies the number of bytes at the end of msgbuf that should
 * not be considered at all. I.e., they are not checked for existing trailers,
 * and the new signoff will be spliced into the buffer before those bytes.
 */
void append_signoff(struct strbuf *msgbuf, size_t ignore_footer, unsigned flag);

void append_conflicts_hint(struct index_state *istate,
		struct strbuf *msgbuf, enum commit_msg_cleanup_mode cleanup_mode);
enum commit_msg_cleanup_mode get_cleanup_mode(const char *cleanup_arg,
	int use_editor);

void cleanup_message(struct strbuf *msgbuf,
	enum commit_msg_cleanup_mode cleanup_mode, int verbose);

int message_is_empty(const struct strbuf *sb,
		     enum commit_msg_cleanup_mode cleanup_mode);
int template_untouched(const struct strbuf *sb, const char *template_file,
		       enum commit_msg_cleanup_mode cleanup_mode);
int update_head_with_reflog(const struct commit *old_head,
			    const struct object_id *new_head,
			    const char *action, const struct strbuf *msg,
			    struct strbuf *err);
void commit_post_rewrite(struct repository *r,
			 const struct commit *current_head,
			 const struct object_id *new_head);

void create_autostash(struct repository *r, const char *path,
		      const char *default_reflog_action);
int apply_autostash(const char *path);

#define SUMMARY_INITIAL_COMMIT   (1 << 0)
#define SUMMARY_SHOW_AUTHOR_DATE (1 << 1)
void print_commit_summary(struct repository *repo,
			  const char *prefix,
			  const struct object_id *oid,
			  unsigned int flags);

/*
 * Reads a file that was presumably written by a shell script, i.e. with an
 * end-of-line marker that needs to be stripped.
 *
 * Note that only the last end-of-line marker is stripped, consistent with the
 * behavior of "$(cat path)" in a shell script.
 *
 * Returns 1 if the file was read, 0 if it could not be read.
 */
int read_oneliner(struct strbuf *buf, const char *path,
		  int skip_if_empty, int warn_nonexistence);
int read_author_script(const char *path, char **name, char **email, char **date,
		       int allow_missing);
void parse_strategy_opts(struct replay_opts *opts, char *raw_opts);
int write_basic_state(struct replay_opts *opts, const char *head_name,
		      struct commit *onto, const char *orig_head);
void sequencer_post_commit_cleanup(struct repository *r, int verbose);
int sequencer_get_last_command(struct repository* r,
			       enum replay_action *action);
int sequencer_determine_whence(struct repository *r, enum commit_whence *whence,
			       int amending);
#endif /* SEQUENCER_H */
