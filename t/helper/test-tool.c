#include "git-compat-util.h"
#include "test-tool.h"
#include "trace2.h"
#include "parse-options.h"

static const char * const test_tool_usage[] = {
	"test-tool [-C <directory>] <command [<arguments>...]]",
	NULL
};

struct test_cmd {
	const char *name;
	int (*fn)(int argc, const char **argv);
};

static struct test_cmd cmds[] = {
	{ "advise", cmd__advise_if_enabled },
	{ "bitmap", cmd__bitmap },
	{ "bloom", cmd__bloom },
	{ "chmtime", cmd__chmtime },
	{ "config", cmd__config },
	{ "crontab", cmd__crontab },
	{ "csprng", cmd__csprng },
	{ "ctype", cmd__ctype },
	{ "date", cmd__date },
	{ "delta", cmd__delta },
	{ "dir-iterator", cmd__dir_iterator },
	{ "drop-caches", cmd__drop_caches },
	{ "dump-cache-tree", cmd__dump_cache_tree },
	{ "dump-fsmonitor", cmd__dump_fsmonitor },
	{ "dump-split-index", cmd__dump_split_index },
	{ "dump-untracked-cache", cmd__dump_untracked_cache },
	{ "example-decorate", cmd__example_decorate },
	{ "fast-rebase", cmd__fast_rebase },
	{ "fsmonitor-client", cmd__fsmonitor_client },
	{ "genrandom", cmd__genrandom },
	{ "genzeros", cmd__genzeros },
	{ "getcwd", cmd__getcwd },
	{ "hashmap", cmd__hashmap },
	{ "hash-speed", cmd__hash_speed },
	{ "hexdump", cmd__hexdump },
	{ "index-version", cmd__index_version },
	{ "json-writer", cmd__json_writer },
	{ "lazy-init-name-hash", cmd__lazy_init_name_hash },
	{ "match-trees", cmd__match_trees },
	{ "mergesort", cmd__mergesort },
	{ "mktemp", cmd__mktemp },
	{ "oid-array", cmd__oid_array },
	{ "oidmap", cmd__oidmap },
	{ "oidtree", cmd__oidtree },
	{ "online-cpus", cmd__online_cpus },
	{ "pack-mtimes", cmd__pack_mtimes },
	{ "parse-options", cmd__parse_options },
	{ "parse-pathspec-file", cmd__parse_pathspec_file },
	{ "partial-clone", cmd__partial_clone },
	{ "path-utils", cmd__path_utils },
	{ "pcre2-config", cmd__pcre2_config },
	{ "pkt-line", cmd__pkt_line },
	{ "prio-queue", cmd__prio_queue },
	{ "proc-receive", cmd__proc_receive },
	{ "progress", cmd__progress },
	{ "reach", cmd__reach },
	{ "read-cache", cmd__read_cache },
	{ "read-graph", cmd__read_graph },
	{ "read-midx", cmd__read_midx },
	{ "ref-store", cmd__ref_store },
	{ "reftable", cmd__reftable },
	{ "rot13-filter", cmd__rot13_filter },
	{ "dump-reftable", cmd__dump_reftable },
	{ "regex", cmd__regex },
	{ "repository", cmd__repository },
	{ "revision-walking", cmd__revision_walking },
	{ "run-command", cmd__run_command },
	{ "scrap-cache-tree", cmd__scrap_cache_tree },
	{ "serve-v2", cmd__serve_v2 },
	{ "sha1", cmd__sha1 },
	{ "sha256", cmd__sha256 },
	{ "sigchain", cmd__sigchain },
	{ "simple-ipc", cmd__simple_ipc },
	{ "strcmp-offset", cmd__strcmp_offset },
	{ "string-list", cmd__string_list },
	{ "submodule-config", cmd__submodule_config },
	{ "submodule-nested-repo-config", cmd__submodule_nested_repo_config },
	{ "subprocess", cmd__subprocess },
	{ "trace2", cmd__trace2 },
	{ "userdiff", cmd__userdiff },
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
	const char *working_directory = NULL;
	struct option options[] = {
		OPT_STRING('C', NULL, &working_directory, "directory",
			   "change the working directory"),
		OPT_END()
	};

	BUG_exit_code = 99;
	argc = parse_options(argc, argv, NULL, options, test_tool_usage,
			     PARSE_OPT_STOP_AT_NON_OPTION |
			     PARSE_OPT_KEEP_ARGV0);

	if (argc < 2)
		die_usage();

	if (working_directory && chdir(working_directory) < 0)
		die("Could not cd to '%s'", working_directory);

	for (i = 0; i < ARRAY_SIZE(cmds); i++) {
		if (!strcmp(cmds[i].name, argv[1])) {
			argv++;
			argc--;
			trace2_cmd_name(cmds[i].name);
			trace2_cmd_list_config();
			trace2_cmd_list_env_vars();
			return cmds[i].fn(argc, argv);
		}
	}
	error("there is no tool named '%s'", argv[1]);
	die_usage();
}
