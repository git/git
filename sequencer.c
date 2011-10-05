#include "cache.h"
#include "sequencer.h"
#include "strbuf.h"
#include "dir.h"

void remove_sequencer_state(int aggressive)
{
	struct strbuf seq_dir = STRBUF_INIT;
	struct strbuf seq_old_dir = STRBUF_INIT;

	strbuf_addf(&seq_dir, "%s", git_path(SEQ_DIR));
	strbuf_addf(&seq_old_dir, "%s", git_path(SEQ_OLD_DIR));
	remove_dir_recursively(&seq_old_dir, 0);
	rename(git_path(SEQ_DIR), git_path(SEQ_OLD_DIR));
	if (aggressive)
		remove_dir_recursively(&seq_old_dir, 0);
	strbuf_release(&seq_dir);
	strbuf_release(&seq_old_dir);
}
