#ifndef BUNDLE_H
#define BUNDLE_H

#include "strvec.h"
#include "string-list.h"
#include "list-objects-filter-options.h"

struct bundle_header {
	unsigned version;
	struct string_list prerequisites;
	struct string_list references;
	const struct git_hash_algo *hash_algo;
	struct list_objects_filter_options filter;
};

#define BUNDLE_HEADER_INIT \
{ \
	.prerequisites = STRING_LIST_INIT_DUP, \
	.references = STRING_LIST_INIT_DUP, \
	.filter = LIST_OBJECTS_FILTER_INIT, \
}
void bundle_header_init(struct bundle_header *header);
void bundle_header_release(struct bundle_header *header);

int is_bundle(const char *path, int quiet);
int read_bundle_header(const char *path, struct bundle_header *header);
int read_bundle_header_fd(int fd, struct bundle_header *header,
			  const char *report_path);
int create_bundle(struct repository *r, const char *path,
		  int argc, const char **argv, struct strvec *pack_options,
		  int version);

enum verify_bundle_flags {
	VERIFY_BUNDLE_VERBOSE = (1 << 0),
	VERIFY_BUNDLE_QUIET = (1 << 1),
	VERIFY_BUNDLE_FSCK = (1 << 2),
};

int verify_bundle(struct repository *r, struct bundle_header *header,
		  enum verify_bundle_flags flags);

struct unbundle_opts {
	enum verify_bundle_flags flags;
	/*
	 * fsck_msg_types may optionally contain fsck message severity
	 * configuration. If present, this configuration gets directly appended
	 * to a '--fsck-objects' option and therefore must be prefixed with '='.
	 * (E.g. "=missingEmail=ignore,gitmodulesUrl=ignore")
	 */
	const char *fsck_msg_types;
};

/**
 * Unbundle after reading the header with read_bundle_header().
 *
 * We'll invoke "git index-pack --stdin --fix-thin" for you on the
 * provided `bundle_fd` from read_bundle_header().
 *
 * Provide "extra_index_pack_args" to pass any extra arguments
 * (e.g. "-v" for verbose/progress), NULL otherwise. The provided
 * "extra_index_pack_args" (if any) will be strvec_clear()'d for you.
 *
 * Before unbundling, this method will call verify_bundle() with 'flags'
 * provided in 'opts'.
 */
int unbundle(struct repository *r, struct bundle_header *header,
	     int bundle_fd, struct strvec *extra_index_pack_args,
	     struct unbundle_opts *opts);
int list_bundle_refs(struct bundle_header *header,
		int argc, const char **argv);

#endif
