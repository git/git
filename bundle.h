#ifndef BUNDLE_H
#define BUNDLE_H

#include "strvec.h"
#include "cache.h"
#include "string-list.h"

struct bundle_header {
	unsigned version;
	struct string_list prerequisites;
	struct string_list references;
	const struct git_hash_algo *hash_algo;
};

#define BUNDLE_HEADER_INIT \
{ \
	.prerequisites = STRING_LIST_INIT_DUP, \
	.references = STRING_LIST_INIT_DUP, \
}
void bundle_header_init(struct bundle_header *header);
void bundle_header_release(struct bundle_header *header);

int is_bundle(const char *path, int quiet);
int read_bundle_header(const char *path, struct bundle_header *header);
int create_bundle(struct repository *r, const char *path,
		  int argc, const char **argv, struct strvec *pack_options,
		  int version);
int verify_bundle(struct repository *r, struct bundle_header *header, int verbose);
#define BUNDLE_VERBOSE 1
int unbundle(struct repository *r, struct bundle_header *header,
	     int bundle_fd, int flags);
int list_bundle_refs(struct bundle_header *header,
		int argc, const char **argv);

#endif
