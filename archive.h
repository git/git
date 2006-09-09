#ifndef ARCHIVE_H
#define ARCHIVE_H

#define MAX_EXTRA_ARGS	32
#define MAX_ARGS	(MAX_EXTRA_ARGS + 32)

struct archiver_args {
	const char *base;
	struct tree *tree;
	const unsigned char *commit_sha1;
	time_t time;
	const char **pathspec;
	void *extra;
};

typedef int (*write_archive_fn_t)(struct archiver_args *);

typedef void *(*parse_extra_args_fn_t)(int argc, const char **argv);

struct archiver {
	const char *name;
	const char *remote;
	struct archiver_args args;
	write_archive_fn_t write_archive;
	parse_extra_args_fn_t parse_extra;
};

extern struct archiver archivers[];

extern int parse_archive_args(int argc,
			      const char **argv,
			      struct archiver *ar);

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
extern void *parse_extra_zip_args(int argc, const char **argv);

#endif	/* ARCHIVE_H */
