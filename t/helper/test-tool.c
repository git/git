#include "git-compat-util.h"
#include "test-tool.h"
#include "trace2.h"

struct test_cmd {
	const char *name;
	int (*fn)(int argc, const char **argv);
};

static struct test_cmd cmds[] = {
	{ "chmtime", cmd__chmtime },
	{ "config", cmd__config },
	{ "ctype", cmd__ctype },
	{ "date", cmd__date },
	{ "delta", cmd__delta },
	{ "drop-caches", cmd__drop_caches },
	{ "dump-cache-tree", cmd__dump_cache_tree },
	{ "dump-fsmonitor", cmd__dump_fsmonitor },
	{ "dump-split-index", cmd__dump_split_index },
	{ "dump-untracked-cache", cmd__dump_untracked_cache },
	{ "example-decorate", cmd__example_decorate },
	{ "genrandom", cmd__genrandom },
	{ "hashmap", cmd__hashmap },
	{ "hash-speed", cmd__hash_speed },
	{ "index-version", cmd__index_version },
	{ "json-writer", cmd__json_writer },
	{ "lazy-init-name-hash", cmd__lazy_init_name_hash },
	{ "match-trees", cmd__match_trees },
	{ "mergesort", cmd__mergesort },
	{ "mktemp", cmd__mktemp },
	{ "online-cpus", cmd__online_cpus },
	{ "parse-options", cmd__parse_options },
	{ "path-utils", cmd__path_utils },
	{ "pkt-line", cmd__pkt_line },
	{ "prio-queue", cmd__prio_queue },
	{ "reach", cmd__reach },
	{ "read-cache", cmd__read_cache },
	{ "read-midx", cmd__read_midx },
	{ "ref-store", cmd__ref_store },
	{ "regex", cmd__regex },
	{ "repository", cmd__repository },
	{ "revision-walking", cmd__revision_walking },
	{ "run-command", cmd__run_command },
	{ "scrap-cache-tree", cmd__scrap_cache_tree },
	{ "sha1", cmd__sha1 },
	{ "sha1-array", cmd__sha1_array },
	{ "sha256", cmd__sha256 },
	{ "sigchain", cmd__sigchain },
	{ "strcmp-offset", cmd__strcmp_offset },
	{ "string-list", cmd__string_list },
	{ "submodule-config", cmd__submodule_config },
	{ "submodule-nested-repo-config", cmd__submodule_nested_repo_config },
	{ "subprocess", cmd__subprocess },
	{ "trace2", cmd__trace2 },
	{ "urlmatch-normalization", cmd__urlmatch_normalization },
	{ "xml-encode", cmd__xml_encode },
	{ "wildmatch", cmd__wildmatch },
#ifdef GIT_WINDOWS_NATIVE
	{ "windows-named-pipe", cmd__windows_named_pipe },
#endif
	{ "write-cache", cmd__write_cache },
};

static NORETURN void die_usage(void)
{
	size_t i;

	fprintf(stderr, "usage: test-tool <toolname> [args]\n");
	for (i = 0; i < ARRAY_SIZE(cmds); i++)
		fprintf(stderr, "  %s\n", cmds[i].name);
	exit(128);
}

int cmd_main(int argc, const char **argv)
{
	int i;

	BUG_exit_code = 99;
	if (argc < 2)
		die_usage();

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (!strcmp(cmds[i].name, argv[1])) {
			argv++;
			argc--;
			trace2_cmd_name(cmds[i].name);
			trace2_cmd_list_config();
			return cmds[i].fn(argc, argv);
		}
	}
	error("there is no tool named '%s'", argv[1]);
	die_usage();
}
