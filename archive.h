#ifndef ARCHIVE_H
#define ARCHIVE_H

#include "cache.h"
#include "pathspec.h"

struct repository;

struct archiver_args {
	struct repository *repo;
	const char *refname;
	const char *base;
	size_t baselen;
	struct tree *tree;
	const struct object_id *commit_oid;
	const struct commit *commit;
	timestamp_t time;
	struct pathspec pathspec;
	unsigned int verbose : 1;
	unsigned int worktree_attributes : 1;
	unsigned int convert : 1;
	int compression_level;
};

/* main api */

int write_archive(int argc, const char **argv, const char *prefix,
		  struct repository *repo,
		  const char *name_hint, int remote);

const char *archive_format_from_filename(const char *filename);

/* archive backend stuff */

#define ARCHIVER_WANT_COMPRESSION_LEVELS 1
#define ARCHIVER_REMOTE 2
struct archiver {
	const char *name;
	int (*write_archive)(const struct archiver *, struct archiver_args *);
	unsigned flags;
	void *data;
};
void register_archiver(struct archiver *);

void init_tar_archiver(void);
void init_zip_archiver(void);
void init_archivers(void);

typedef int (*write_archive_entry_fn_t)(struct archiver_args *args,
					const struct object_id *oid,
					const char *path, size_t pathlen,
					unsigned int mode);

int write_archive_entries(struct archiver_args *args, write_archive_entry_fn_t write_entry);
void *object_file_to_archive(const struct archiver_args *args,
			     const char *path, const struct object_id *oid,
			     unsigned int mode, enum object_type *type,
			     unsigned long *sizep);

#endif	/* ARCHIVE_H */
