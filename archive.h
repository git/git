#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "cache.h"
#include "pathspec.h"

struct repository;
struct pretty_print_context;

struct archiver_args {
	struct repository *repo;
	char *refname;
	const char *prefix;
	const char *base;
	size_t baselen;
	struct tree *tree;
	const struct object_id *commit_oid;
	const struct commit *commit;
	const char *mtime_option;
	timestamp_t time;
	struct pathspec pathspec;
	unsigned int verbose : 1;
	unsigned int worktree_attributes : 1;
	unsigned int convert : 1;
	int compression_level;
	struct string_list extra_files;
	struct pretty_print_context *pretty_ctx;
};

/* main api */

int write_archive(int argc, const char **argv, const char *prefix,
		  struct repository *repo,
		  const char *name_hint, int remote);

const char *archive_format_from_filename(const char *filename);

/* archive backend stuff */

#define ARCHIVER_WANT_COMPRESSION_LEVELS 1
#define ARCHIVER_REMOTE 2
#define ARCHIVER_HIGH_COMPRESSION_LEVELS 4
struct archiver {
	const char *name;
	int (*write_archive)(const struct archiver *, struct archiver_args *);
	unsigned flags;
	char *filter_command;
};
void register_archiver(struct archiver *);

void init_tar_archiver(void);
void init_zip_archiver(void);
void init_archivers(void);

typedef int (*write_archive_entry_fn_t)(struct archiver_args *args,
					const struct object_id *oid,
					const char *path, size_t pathlen,
					unsigned int mode,
					void *buffer, unsigned long size);

int write_archive_entries(struct archiver_args *args, write_archive_entry_fn_t write_entry);

#endif	/* ARCHIVE_H */
