#ifndef TEST_TOOL_H
#define TEST_TOOL_H

#define USE_THE_INDEX_COMPATIBILITY_MACROS
#include "git-compat-util.h"

int cmd__advise_if_enabled(int argc, const char **argv);
int cmd__bitmap(int argc, const char **argv);
int cmd__bloom(int argc, const char **argv);
int cmd__chmtime(int argc, const char **argv);
int cmd__config(int argc, const char **argv);
int cmd__crontab(int argc, const char **argv);
int cmd__csprng(int argc, const char **argv);
int cmd__ctype(int argc, const char **argv);
int cmd__date(int argc, const char **argv);
int cmd__delta(int argc, const char **argv);
int cmd__dir_iterator(int argc, const char **argv);
int cmd__drop_caches(int argc, const char **argv);
int cmd__dump_cache_tree(int argc, const char **argv);
int cmd__dump_fsmonitor(int argc, const char **argv);
int cmd__dump_split_index(int argc, const char **argv);
int cmd__dump_untracked_cache(int argc, const char **argv);
int cmd__dump_reftable(int argc, const char **argv);
int cmd__example_decorate(int argc, const char **argv);
int cmd__fast_rebase(int argc, const char **argv);
int cmd__fsmonitor_client(int argc, const char **argv);
int cmd__genrandom(int argc, const char **argv);
int cmd__genzeros(int argc, const char **argv);
int cmd__getcwd(int argc, const char **argv);
int cmd__hashmap(int argc, const char **argv);
int cmd__hash_speed(int argc, const char **argv);
int cmd__hexdump(int argc, const char **argv);
int cmd__index_version(int argc, const char **argv);
int cmd__json_writer(int argc, const char **argv);
int cmd__lazy_init_name_hash(int argc, const char **argv);
int cmd__match_trees(int argc, const char **argv);
int cmd__mergesort(int argc, const char **argv);
int cmd__mktemp(int argc, const char **argv);
int cmd__oidmap(int argc, const char **argv);
int cmd__oidtree(int argc, const char **argv);
int cmd__online_cpus(int argc, const char **argv);
int cmd__pack_mtimes(int argc, const char **argv);
int cmd__parse_options(int argc, const char **argv);
int cmd__parse_pathspec_file(int argc, const char** argv);
int cmd__partial_clone(int argc, const char **argv);
int cmd__path_utils(int argc, const char **argv);
int cmd__pcre2_config(int argc, const char **argv);
int cmd__pkt_line(int argc, const char **argv);
int cmd__prio_queue(int argc, const char **argv);
int cmd__proc_receive(int argc, const char **argv);
int cmd__progress(int argc, const char **argv);
int cmd__reach(int argc, const char **argv);
int cmd__read_cache(int argc, const char **argv);
int cmd__read_graph(int argc, const char **argv);
int cmd__read_midx(int argc, const char **argv);
int cmd__ref_store(int argc, const char **argv);
int cmd__rot13_filter(int argc, const char **argv);
int cmd__reftable(int argc, const char **argv);
int cmd__regex(int argc, const char **argv);
int cmd__repository(int argc, const char **argv);
int cmd__revision_walking(int argc, const char **argv);
int cmd__run_command(int argc, const char **argv);
int cmd__scrap_cache_tree(int argc, const char **argv);
int cmd__serve_v2(int argc, const char **argv);
int cmd__sha1(int argc, const char **argv);
int cmd__oid_array(int argc, const char **argv);
int cmd__sha256(int argc, const char **argv);
int cmd__sigchain(int argc, const char **argv);
int cmd__simple_ipc(int argc, const char **argv);
int cmd__strcmp_offset(int argc, const char **argv);
int cmd__string_list(int argc, const char **argv);
int cmd__submodule_config(int argc, const char **argv);
int cmd__submodule_nested_repo_config(int argc, const char **argv);
int cmd__subprocess(int argc, const char **argv);
int cmd__trace2(int argc, const char **argv);
int cmd__userdiff(int argc, const char **argv);
int cmd__urlmatch_normalization(int argc, const char **argv);
int cmd__xml_encode(int argc, const char **argv);
int cmd__wildmatch(int argc, const char **argv);
#ifdef GIT_WINDOWS_NATIVE
int cmd__windows_named_pipe(int argc, const char **argv);
#endif
int cmd__write_cache(int argc, const char **argv);

int cmd_hash_impl(int ac, const char **av, int algo);

#endif
