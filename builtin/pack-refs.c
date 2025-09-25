#include "builtin.h"
#include "gettext.h"
#include "pack-refs.h"

int cmd_pack_refs(int argc,
		  const char **argv,
		  const char *prefix,
		  struct repository *repo)
{
	static char const * const pack_refs_usage[] = {
		N_("git pack-refs " PACK_REFS_OPTS),
		NULL
	};

	return pack_refs_core(argc, argv, prefix, repo, pack_refs_usage);
}
