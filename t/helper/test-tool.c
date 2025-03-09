#include "git-compat-util.h"
#include "test-tool.h"
#include "test-tool-utils.h"
#include "trace2.h"
#include "parse-options.h"

static const char * const test_tool_usage[] = {
	"test-tool [-C <directory>] <command [<arguments>...]]",
	NULL
};

static struct test_cmd cmds[] = {
	{ "advise", cmd__advise_if_enabled },
	{ "bitmap", cmd__bitmap },
	{ "bloom", cmd__bloom },
	{ "bundle-uri", cmd__bundle_uri },
	{ "cache-tree", cmd__cache_tree },
	{ "chmtime", cmd__chmtime },
	{ "config", cmd__config },
	{ "crontab", cmd__crontab },
	{ "csprng", cmd__csprng },
	{ "date", cmd__date },
	{ "delete-gpgsig", cmd__delete_gpgsig },
	{ "delta", cmd__delta },
	{ "dir-iterator", cmd__dir_iterator },
	{ "drop-caches", cmd__drop_caches },
	{ "dump-cache-tree", cmd__dump_cache_tree },
	{ "dump-fsmonitor", cmd__dump_fsmonitor },
	{ "dump-reftable", cmd__dump_reftable },
	{ "dump-split-index", cmd__dump_split_index },
	{ "dump-untracked-cache", cmd__dump_untracked_cache },
	{ "env-helper", cmd__env_helper },
	{ "example-tap", cmd__example_tap },
	{ "find-pack", cmd__find_pack },
	{ "fsmonitor-client", cmd__fsmonitor_client },
	{ "genrandom", cmd__genrandom },
	{ "genzeros", cmd__genzeros },
	{ "getcwd", cmd__getcwd },
	{ "hashmap", cmd__hashmap },
	{ "hash-speed", cmd__hash_speed },
	{ "hexdump", cmd__hexdump },
	{ "json-writer", cmd__json_writer },
	{ "lazy-init-name-hash", cmd__lazy_init_name_hash },
	{ "match-trees", cmd__match_trees },
	{ "mergesort", cmd__mergesort },
	{ "mktemp", cmd__mktemp },
	{ "name-hash", cmd__name_hash },
	{ "online-cpus", cmd__online_cpus },
	{ "pack-mtimes", cmd__pack_mtimes },
	{ "parse-options", cmd__parse_options },
	{ "parse-options-flags", cmd__parse_options_flags },
	{ "parse-pathspec-file", cmd__parse_pathspec_file },
	{ "parse-subcommand", cmd__parse_subcommand },
	{ "partial-clone", cmd__partial_clone },
	{ "path-utils", cmd__path_utils },
	{ "path-walk", cmd__path_walk },
	{ "pcre2-config", cmd__pcre2_config },
	{ "pkt-line", cmd__pkt_line },
	{ "proc-receive", cmd__proc_receive },
	{ "progress", cmd__progress },
	{ "reach", cmd__reach },
	{ "read-cache", cmd__read_cache },
	{ "read-graph", cmd__read_graph },
	{ "read-midx", cmd__read_midx },
	{ "ref-store", cmd__ref_store },
	{ "rot13-filter", cmd__rot13_filter },
	{ "regex", cmd__regex },
	{ "repository", cmd__repository },
	{ "revision-walking", cmd__revision_walking },
	{ "run-command", cmd__run_command },
	{ "scrap-cache-tree", cmd__scrap_cache_tree },
	{ "serve-v2", cmd__serve_v2 },
	{ "sha1", cmd__sha1 },
	{ "sha1-is-sha1dc", cmd__sha1_is_sha1dc },
	{ "sha1-unsafe", cmd__sha1_unsafe },
	{ "sha256", cmd__sha256 },
	{ "sigchain", cmd__sigchain },
	{ "simple-ipc", cmd__simple_ipc },
	{ "string-list", cmd__string_list },
	{ "submodule", cmd__submodule },
	{ "submodule-config", cmd__submodule_config },
	{ "submodule-nested-repo-config", cmd__submodule_nested_repo_config },
	{ "subprocess", cmd__subprocess },
	{ "trace2", cmd__trace2 },
	{ "truncate", cmd__truncate },
	{ "userdiff", cmd__userdiff },
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

	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
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
