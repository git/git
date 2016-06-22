#ifndef BUNDLE_H
#define BUNDLE_H

struct ref_list {
	unsigned int nr, alloc;
	struct ref_list_entry {
		unsigned char sha1[20];
		char *name;
	} *list;
};

struct bundle_header {
	int version;
	char *filename;
	char *datafile;
	size_t size;
	unsigned char csum[GIT_SHA1_RAWSZ];
	struct ref_list prerequisites;
	struct ref_list references;
};

int is_bundle(const char *path, int quiet);
void init_bundle_header(struct bundle_header *, const char *filename);
int read_bundle_header(struct bundle_header *header);
int create_bundle(struct bundle_header *header, const char *path,
		int argc, const char **argv);
int verify_bundle(struct bundle_header *header, int verbose);
#define BUNDLE_VERBOSE 1
int unbundle(struct bundle_header *header, int bundle_fd, int flags);
int list_bundle_refs(struct bundle_header *header,
		int argc, const char **argv);
void release_bundle_header(struct bundle_header *);

#endif
