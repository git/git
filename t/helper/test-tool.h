#ifndef __TEST_TOOL_H__
#define __TEST_TOOL_H__

int cmd__chmtime(int argc, const char **argv);
int cmd__config(int argc, const char **argv);
int cmd__ctype(int argc, const char **argv);
int cmd__date(int argc, const char **argv);
int cmd__delta(int argc, const char **argv);
int cmd__drop_caches(int argc, const char **argv);
int cmd__dump_cache_tree(int argc, const char **argv);
int cmd__dump_split_index(int argc, const char **argv);
int cmd__example_decorate(int argc, const char **argv);
int cmd__genrandom(int argc, const char **argv);
int cmd__hashmap(int argc, const char **argv);
int cmd__lazy_init_name_hash(int argc, const char **argv);
int cmd__sha1(int argc, const char **argv);

#endif
