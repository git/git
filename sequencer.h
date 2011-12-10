#ifndef SEQUENCER_H
#define SEQUENCER_H

#define SEQ_DIR		"sequencer"
#define SEQ_HEAD_FILE	"sequencer/head"
#define SEQ_TODO_FILE	"sequencer/todo"
#define SEQ_OPTS_FILE	"sequencer/opts"

/* Removes SEQ_DIR. */
extern void remove_sequencer_state(void);

#endif
