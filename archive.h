#ifndef ARCHIVE_H
#define ARCHIVE_H

#define MAX_EXTRA_ARGS	32
#define MAX_ARGS	(MAX_EXTRA_ARGS + 32)

struct archiver_args {
	const char *base;
	size_t baselen;
	struct tree *tree;
	const unsigned char *commit_sha1;
	const struct commit *commit;
	time_t time;
	const char **pathspec;
	unsigned int verbose : 1;
	int compression_level;
};

typedef int (*write_archive_fn_t)(struct archiver_args *);

typedef int (*write_archive_entry_fn_t)(struct archiver_args *args, const unsigned char *sha1, const char *path, size_t pathlen, unsigned int mode, void *buffer, unsigned long size);

struct archiver {
	const char *name;
	write_archive_fn_t write_archive;
	unsigned int flags;
};

extern int parse_archive_args(int argc, const char **argv, const struct archiver **ar, struct archiver_args *args);

extern void parse_treeish_arg(const char **treeish,
			      struct archiver_args *ar_args,
			      const char *prefix);

extern void parse_pathspec_arg(const char **pathspec,
			       struct archiver_args *args);
/*
 * Archive-format specific backends.
 */
extern int write_tar_archive(struct archiver_args *);
extern int write_zip_archive(struct archiver_args *);

extern int write_archive_entries(struct archiver_args *args, write_archive_entry_fn_t write_entry);

#endif	/* ARCHIVE_H */
