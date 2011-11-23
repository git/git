#ifndef ARCHIVE_H
#define ARCHIVE_H

struct archiver_args {
	const char *base;
	size_t baselen;
	struct tree *tree;
	const unsigned char *commit_sha1;
	const struct commit *commit;
	time_t time;
	const char **pathspec;
	unsigned int verbose : 1;
	unsigned int worktree_attributes : 1;
	int compression_level;
};

#define ARCHIVER_WANT_COMPRESSION_LEVELS 1
#define ARCHIVER_REMOTE 2
struct archiver {
	const char *name;
	int (*write_archive)(const struct archiver *, struct archiver_args *);
	unsigned flags;
	void *data;
};
extern void register_archiver(struct archiver *);

extern void init_tar_archiver(void);
extern void init_zip_archiver(void);

typedef int (*write_archive_entry_fn_t)(struct archiver_args *args, const unsigned char *sha1, const char *path, size_t pathlen, unsigned int mode, void *buffer, unsigned long size);

extern int write_archive_entries(struct archiver_args *args, write_archive_entry_fn_t write_entry);
extern int write_archive(int argc, const char **argv, const char *prefix, int setup_prefix, const char *name_hint, int remote);

const char *archive_format_from_filename(const char *filename);

#endif	/* ARCHIVE_H */
