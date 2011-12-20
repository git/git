#include "cache.h"
#include "sequencer.h"
#include "strbuf.h"
#include "dir.h"

void remove_sequencer_state(void)
{
	struct strbuf seq_dir = STRBUF_INIT;

	strbuf_addf(&seq_dir, "%s", git_path(SEQ_DIR));
	remove_dir_recursively(&seq_dir, 0);
	strbuf_release(&seq_dir);
}
