#include <time.h>
#include "cache.h"

#define RECORDSIZE	(512)
#define BLOCKSIZE	(RECORDSIZE * 20)

static const char *tar_tree_usage = "tar-tree <key> [basedir]";

static char block[BLOCKSIZE];
static unsigned long offset;

static const char *basedir;
static time_t archive_time;

struct path_prefix {
	struct path_prefix *prev;
	const char *name;
};

/* tries hard to write, either succeeds or dies in the attempt */
static void reliable_write(void *buf, unsigned long size)
{
	while (size > 0) {
		long ret = write(1, buf, size);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			if (errno == EPIPE)
				exit(0);
			die("tar-tree: %s", strerror(errno));
		} else if (!ret) {
			die("tar-tree: disk full?");
		}
		size -= ret;
		buf += ret;
	}
}

/* writes out the whole block, but only if it is full */
static void write_if_needed(void)
{
	if (offset == BLOCKSIZE) {
		reliable_write(block, BLOCKSIZE);
		offset = 0;
	}
}

/*
 * The end of tar archives is marked by 1024 nul bytes and after that
 * follows the rest of the block (if any).
 */
static void write_trailer(void)
{
	memset(block + offset, 0, RECORDSIZE);
	offset += RECORDSIZE;
	write_if_needed();
	memset(block + offset, 0, RECORDSIZE);
	offset += RECORDSIZE;
	write_if_needed();
	if (offset) {
		memset(block + offset, 0, BLOCKSIZE - offset);
		reliable_write(block, BLOCKSIZE);
		offset = 0;
	}
}

/*
 * queues up writes, so that all our write(2) calls write exactly one
 * full block; pads writes to RECORDSIZE
 */
static void write_blocked(void *buf, unsigned long size)
{
	unsigned long tail;

	if (offset) {
		unsigned long chunk = BLOCKSIZE - offset;
		if (size < chunk)
			chunk = size;
		memcpy(block + offset, buf, chunk);
		size -= chunk;
		offset += chunk;
		buf += chunk;
		write_if_needed();
	}
	while (size >= BLOCKSIZE) {
		reliable_write(buf, BLOCKSIZE);
		size -= BLOCKSIZE;
		buf += BLOCKSIZE;
	}
	if (size) {
		memcpy(block + offset, buf, size);
		buf += size;
		offset += size;
	}
	tail = offset % RECORDSIZE;
	if (tail)  {
		memset(block + offset, 0, RECORDSIZE - tail);
		offset += RECORDSIZE - tail;
	}
	write_if_needed();
}

static void append_string(char **p, const char *s)
{
	unsigned int len = strlen(s);
	memcpy(*p, s, len);
	*p += len;
}

static void append_char(char **p, char c)
{
	**p = c;
	*p += 1;
}

static void append_long(char **p, long n)
{
	int len = sprintf(*p, "%ld", n);
	*p += len;
}

static void append_path_prefix(char **buffer, struct path_prefix *prefix)
{
	if (!prefix)
		return;
	append_path_prefix(buffer, prefix->prev);
	append_string(buffer, prefix->name);
	append_char(buffer, '/');
}

static unsigned int path_prefix_len(struct path_prefix *prefix)
{
	if (!prefix)
		return 0;
	return path_prefix_len(prefix->prev) + strlen(prefix->name) + 1;
}

static void append_path(char **p, int is_dir, const char *basepath,
                        struct path_prefix *prefix, const char *path)
{
	if (basepath) {
		append_string(p, basepath);
		append_char(p, '/');
	}
	append_path_prefix(p, prefix);
	append_string(p, path);
	if (is_dir)
		append_char(p, '/');
}

static unsigned int path_len(int is_dir, const char *basepath,
                             struct path_prefix *prefix, const char *path)
{
	unsigned int len = 0;
	if (basepath)
		len += strlen(basepath) + 1;
	len += path_prefix_len(prefix) + strlen(path);
	if (is_dir)
		len++;
	return len;
}

static void write_header(const char *, char, const char *, struct path_prefix *,
                         const char *, unsigned int, unsigned long);

/* stores a pax extended header directly in the block buffer */
static void write_extended_header(const char *headerfilename, int is_dir,
                                  const char *basepath,
                                  struct path_prefix *prefix,
                                  const char *path, unsigned int namelen)
{
	char *p;
	unsigned int size = 1 + 6 + namelen + 1;
	if (size > 9)
		size++;
	if (size > 99)
		size++;
	if (size > RECORDSIZE)
		die("tar-tree: extended header too big, wtf?");
	write_header(NULL, 'x', NULL, NULL, headerfilename, 0100600, size);
	p = block + offset;
	memset(p, 0, RECORDSIZE);
	offset += RECORDSIZE;
	append_long(&p, size);
	append_string(&p, " path=");
	append_path(&p, is_dir, basepath, prefix, path);
	append_char(&p, '\n');
	write_if_needed();
}

static void write_global_extended_header(const char *sha1)
{
	char *p;
	write_header(NULL, 'g', NULL, NULL, "pax_global_header", 0, 52);
	p = block + offset;
	memset(p, 0, RECORDSIZE);
	offset += RECORDSIZE;
	append_long(&p, 52);	/* 2 + 9 + 40 + 1 */
	append_string(&p, " comment=");
	append_string(&p, sha1_to_hex(sha1));
	append_char(&p, '\n');
	write_if_needed();
}

/* stores a ustar header directly in the block buffer */
static void write_header(const char *sha1, char typeflag, const char *basepath,
                         struct path_prefix *prefix, const char *path,
                         unsigned int mode, unsigned long size)
{
	unsigned int namelen; 
	char *p, *header = NULL;
	unsigned int checksum = 0;
	int i;

	namelen = path_len(S_ISDIR(mode), basepath, prefix, path);
	if (namelen > 500) {
		die("tar-tree: name too log of object %s\n", sha1_to_hex(sha1));
	} else if (namelen > 100) {
		char *sha1_hex = sha1_to_hex(sha1);
		char headerfilename[51];
		sprintf(headerfilename, "%s.paxheader", sha1_hex);
		/* the extended header must be written before the normal one */
		write_extended_header(headerfilename, S_ISDIR(mode), basepath,
				      prefix, path, namelen);

		header = block + offset;
		memset(header, 0, RECORDSIZE);
		offset += RECORDSIZE;
		sprintf(header, "%s.data", sha1_hex);
	} else {
		header = block + offset;
		memset(header, 0, RECORDSIZE);
		offset += RECORDSIZE;
		p = header;
		append_path(&p, S_ISDIR(mode), basepath, prefix, path);
	}

	if (S_ISDIR(mode))
		mode |= 0755;	/* GIT doesn't store permissions of dirs */
	sprintf(&header[100], "%07o", mode & 07777);

	/* XXX: should we provide more meaningful info here? */
	sprintf(&header[108], "%07o", 0);	/* uid */
	sprintf(&header[116], "%07o", 0);	/* gid */
	strncpy(&header[265], "git", 31);	/* uname */
	strncpy(&header[297], "git", 31);	/* gname */

	sprintf(&header[124], "%011lo", S_ISDIR(mode) ? 0 : size);
	sprintf(&header[136], "%011lo", archive_time);

	header[156] = typeflag;

	memcpy(&header[257], "ustar", 6);
	memcpy(&header[263], "00", 2);

	printf(&header[329], "%07o", 0);	/* devmajor */
	printf(&header[337], "%07o", 0);	/* devminor */

	memset(&header[148], ' ', 8);
	for (i = 0; i < RECORDSIZE; i++)
		checksum += header[i];
	sprintf(&header[148], "%07o", checksum & 0x1fffff);

	write_if_needed();
}

static void traverse_tree(void *buffer, unsigned long size,
			  struct path_prefix *prefix)
{
	struct path_prefix this_prefix;
	this_prefix.prev = prefix;

	while (size) {
		int namelen = strlen(buffer)+1;
		void *eltbuf;
		char elttype[20];
		unsigned long eltsize;
		unsigned char *sha1 = buffer + namelen;
		char *path = strchr(buffer, ' ') + 1;
		unsigned int mode;

		if (size < namelen + 20 || sscanf(buffer, "%o", &mode) != 1)
			die("corrupt 'tree' file");
		buffer = sha1 + 20;
		size -= namelen + 20;

		eltbuf = read_sha1_file(sha1, elttype, &eltsize);
		if (!eltbuf)
			die("cannot read %s", sha1_to_hex(sha1));
		write_header(sha1, S_ISDIR(mode) ? '5' : '0', basedir,
		             prefix, path, mode, eltsize);
		if (!strcmp(elttype, "tree")) {
			this_prefix.name = path;
			traverse_tree(eltbuf, eltsize, &this_prefix);
		} else if (!strcmp(elttype, "blob")) {
			write_blocked(eltbuf, eltsize);
		}
		free(eltbuf);
	}
}

/* get commit time from committer line of commit object */
time_t commit_time(void * buffer, unsigned long size)
{
	time_t result = 0;
	char *p = buffer;

	while (size > 0) {
		char *endp = memchr(p, '\n', size);
		if (!endp || endp == p)
			break;
		*endp = '\0';
		if (endp - p > 10 && !memcmp(p, "committer ", 10)) {
			char *nump = strrchr(p, '>');
			if (!nump)
				break;
			nump++;
			result = strtoul(nump, &endp, 10);
			if (*endp != ' ')
				result = 0;
			break;
		}
		size -= endp - p - 1;
		p = endp + 1;
	}
	return result;
}

int main(int argc, char **argv)
{
	unsigned char sha1[20];
	unsigned char commit_sha1[20];
	void *buffer;
	unsigned long size;

	switch (argc) {
	case 3:
		basedir = argv[2];
		/* FALLTHROUGH */
	case 2:
		if (get_sha1(argv[1], sha1) < 0)
			usage(tar_tree_usage);
		break;
	default:
		usage(tar_tree_usage);
	}

	sha1_file_directory = getenv(DB_ENVIRONMENT);
	if (!sha1_file_directory)
		sha1_file_directory = DEFAULT_DB_ENVIRONMENT;

	buffer = read_object_with_reference(sha1, "commit", &size, commit_sha1);
	if (buffer) {
		write_global_extended_header(commit_sha1);
		archive_time = commit_time(buffer, size);
		free(buffer);
	}
	buffer = read_object_with_reference(sha1, "tree", &size, NULL);
	if (!buffer)
		die("not a reference to a tag, commit or tree object: %s",
		    sha1_to_hex(sha1));
	if (!archive_time)
		archive_time = time(NULL);
	if (basedir)
		write_header("0", '5', NULL, NULL, basedir, 040755, 0);
	traverse_tree(buffer, size, NULL);
	free(buffer);
	write_trailer();
	return 0;
}
