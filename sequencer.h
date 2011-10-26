#ifndef SEQUENCER_H
#define SEQUENCER_H

#define SEQ_DIR		"sequencer"
#define SEQ_OLD_DIR	"sequencer-old"
#define SEQ_HEAD_FILE	"sequencer/head"
#define SEQ_TODO_FILE	"sequencer/todo"
#define SEQ_OPTS_FILE	"sequencer/opts"

enum replay_action { REPLAY_REVERT, REPLAY_PICK };

struct replay_insn_list {
	enum replay_action action;
	struct commit *operand;
	struct replay_insn_list *next;
};

/*
 * Removes SEQ_OLD_DIR and renames SEQ_DIR to SEQ_OLD_DIR, ignoring
 * any errors.  Intended to be used by 'git reset'.
 *
 * With the aggressive flag, it additionally removes SEQ_OLD_DIR,
 * ignoring any errors.  Inteded to be used by the sequencer's
 * '--reset' subcommand.
 */
void remove_sequencer_state(int aggressive);

#endif
