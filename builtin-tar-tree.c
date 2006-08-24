/*
 * Copyright (c) 2005, 2006 Rene Scharfe
 */
#include <time.h>
#include "cache.h"
#include "tree-walk.h"
#include "commit.h"
#include "strbuf.h"
#include "tar.h"
#include "builtin.h"
#include "pkt-line.h"

#define RECORDSIZE	(512)
#define BLOCKSIZE	(RECORDSIZE * 20)

static const char tar_tree_usage[] =
"git-tar-tree [--remote=<repo>] <tree-ish> [basedir]";

static char block[BLOCKSIZE];
static unsigned long offset;

static time_t archive_time;
static int tar_umask;

/* writes out the whole block, but only if it is full */
static void write_if_needed(void)
{
	if (offset == BLOCKSIZE) {
		write_or_die(1, block, BLOCKSIZE);
		offset = 0;
	}
}

/*
 * queues up writes, so that all our write(2) calls write exactly one
 * full block; pads writes to RECORDSIZE
 */
static void write_blocked(const void *data, unsigned long size)
{
	const char *buf = data;
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
		write_or_die(1, buf, BLOCKSIZE);
		size -= BLOCKSIZE;
		buf += BLOCKSIZE;
	}
	if (size) {
		memcpy(block + offset, buf, size);
		offset += size;
	}
	tail = offset % RECORDSIZE;
	if (tail)  {
		memset(block + offset, 0, RECORDSIZE - tail);
		offset += RECORDSIZE - tail;
	}
	write_if_needed();
}

/*
 * The end of tar archives is marked by 2*512 nul bytes and after that
 * follows the rest of the block (if any).
 */
static void write_trailer(void)
{
	int tail = BLOCKSIZE - offset;
	memset(block + offset, 0, tail);
	write_or_die(1, block, BLOCKSIZE);
	if (tail < 2 * RECORDSIZE) {
		memset(block, 0, offset);
		write_or_die(1, block, BLOCKSIZE);
	}
}

static void strbuf_append_string(struct strbuf *sb, const char *s)
{
	int slen = strlen(s);
	int total = sb->len + slen;
	if (total > sb->alloc) {
		sb->buf = xrealloc(sb->buf, total);
		sb->alloc = total;
	}
	memcpy(sb->buf + sb->len, s, slen);
	sb->len = total;
}

/*
 * pax extended header records have the format "%u %s=%s\n".  %u contains
 * the size of the whole string (including the %u), the first %s is the
 * keyword, the second one is the value.  This function constructs such a
 * string and appends it to a struct strbuf.
 */
static void strbuf_append_ext_header(struct strbuf *sb, const char *keyword,
                                     const char *value, unsigned int valuelen)
{
	char *p;
	int len, total, tmp;

	/* "%u %s=%s\n" */
	len = 1 + 1 + strlen(keyword) + 1 + valuelen + 1;
	for (tmp = len; tmp > 9; tmp /= 10)
		len++;

	total = sb->len + len;
	if (total > sb->alloc) {
		sb->buf = xrealloc(sb->buf, total);
		sb->alloc = total;
	}

	p = sb->buf;
	p += sprintf(p, "%u %s=", len, keyword);
	memcpy(p, value, valuelen);
	p += valuelen;
	*p = '\n';
	sb->len = total;
}

static unsigned int ustar_header_chksum(const struct ustar_header *header)
{
	char *p = (char *)header;
	unsigned int chksum = 0;
	while (p < header->chksum)
		chksum += *p++;
	chksum += sizeof(header->chksum) * ' ';
	p += sizeof(header->chksum);
	while (p < (char *)header + sizeof(struct ustar_header))
		chksum += *p++;
	return chksum;
}

static int get_path_prefix(const struct strbuf *path, int maxlen)
{
	int i = path->len;
	if (i > maxlen)
		i = maxlen;
	do {
		i--;
	} while (i > 0 && path->buf[i] != '/');
	return i;
}

static void write_entry(const unsigned char *sha1, struct strbuf *path,
                        unsigned int mode, void *buffer, unsigned long size)
{
	struct ustar_header header;
	struct strbuf ext_header;

	memset(&header, 0, sizeof(header));
	ext_header.buf = NULL;
	ext_header.len = ext_header.alloc = 0;

	if (!sha1) {
		*header.typeflag = TYPEFLAG_GLOBAL_HEADER;
		mode = 0100666;
		strcpy(header.name, "pax_global_header");
	} else if (!path) {
		*header.typeflag = TYPEFLAG_EXT_HEADER;
		mode = 0100666;
		sprintf(header.name, "%s.paxheader", sha1_to_hex(sha1));
	} else {
		if (S_ISDIR(mode)) {
			*header.typeflag = TYPEFLAG_DIR;
			mode = (mode | 0777) & ~tar_umask;
		} else if (S_ISLNK(mode)) {
			*header.typeflag = TYPEFLAG_LNK;
			mode |= 0777;
		} else if (S_ISREG(mode)) {
			*header.typeflag = TYPEFLAG_REG;
			mode = (mode | ((mode & 0100) ? 0777 : 0666)) & ~tar_umask;
		} else {
			error("unsupported file mode: 0%o (SHA1: %s)",
			      mode, sha1_to_hex(sha1));
			return;
		}
		if (path->len > sizeof(header.name)) {
			int plen = get_path_prefix(path, sizeof(header.prefix));
			int rest = path->len - plen - 1;
			if (plen > 0 && rest <= sizeof(header.name)) {
				memcpy(header.prefix, path->buf, plen);
				memcpy(header.name, path->buf + plen + 1, rest);
			} else {
				sprintf(header.name, "%s.data",
				        sha1_to_hex(sha1));
				strbuf_append_ext_header(&ext_header, "path",
				                         path->buf, path->len);
			}
		} else
			memcpy(header.name, path->buf, path->len);
	}

	if (S_ISLNK(mode) && buffer) {
		if (size > sizeof(header.linkname)) {
			sprintf(header.linkname, "see %s.paxheader",
			        sha1_to_hex(sha1));
			strbuf_append_ext_header(&ext_header, "linkpath",
			                         buffer, size);
		} else
			memcpy(header.linkname, buffer, size);
	}

	sprintf(header.mode, "%07o", mode & 07777);
	sprintf(header.size, "%011lo", S_ISREG(mode) ? size : 0);
	sprintf(header.mtime, "%011lo", archive_time);

	/* XXX: should we provide more meaningful info here? */
	sprintf(header.uid, "%07o", 0);
	sprintf(header.gid, "%07o", 0);
	strlcpy(header.uname, "git", sizeof(header.uname));
	strlcpy(header.gname, "git", sizeof(header.gname));
	sprintf(header.devmajor, "%07o", 0);
	sprintf(header.devminor, "%07o", 0);

	memcpy(header.magic, "ustar", 6);
	memcpy(header.version, "00", 2);

	sprintf(header.chksum, "%07o", ustar_header_chksum(&header));

	if (ext_header.len > 0) {
		write_entry(sha1, NULL, 0, ext_header.buf, ext_header.len);
		free(ext_header.buf);
	}
	write_blocked(&header, sizeof(header));
	if (S_ISREG(mode) && buffer && size > 0)
		write_blocked(buffer, size);
}

static void write_global_extended_header(const unsigned char *sha1)
{
	struct strbuf ext_header;
	ext_header.buf = NULL;
	ext_header.len = ext_header.alloc = 0;
	strbuf_append_ext_header(&ext_header, "comment", sha1_to_hex(sha1), 40);
	write_entry(NULL, NULL, 0, ext_header.buf, ext_header.len);
	free(ext_header.buf);
}

static void traverse_tree(struct tree_desc *tree, struct strbuf *path)
{
	int pathlen = path->len;
	struct name_entry entry;

	while (tree_entry(tree, &entry)) {
		void *eltbuf;
		char elttype[20];
		unsigned long eltsize;

		eltbuf = read_sha1_file(entry.sha1, elttype, &eltsize);
		if (!eltbuf)
			die("cannot read %s", sha1_to_hex(entry.sha1));

		path->len = pathlen;
		strbuf_append_string(path, entry.path);
		if (S_ISDIR(entry.mode))
			strbuf_append_string(path, "/");

		write_entry(entry.sha1, path, entry.mode, eltbuf, eltsize);

		if (S_ISDIR(entry.mode)) {
			struct tree_desc subtree;
			subtree.buf = eltbuf;
			subtree.size = eltsize;
			traverse_tree(&subtree, path);
		}
		free(eltbuf);
	}
}

static int git_tar_config(const char *var, const char *value)
{
	if (!strcmp(var, "tar.umask")) {
		if (!strcmp(value, "user")) {
			tar_umask = umask(0);
			umask(tar_umask);
		} else {
			tar_umask = git_config_int(var, value);
		}
		return 0;
	}
	return git_default_config(var, value);
}

static int generate_tar(int argc, const char **argv, const char *prefix)
{
	unsigned char sha1[20], tree_sha1[20];
	struct commit *commit;
	struct tree_desc tree;
	struct strbuf current_path;
	void *buffer;

	current_path.buf = xmalloc(PATH_MAX);
	current_path.alloc = PATH_MAX;
	current_path.len = current_path.eof = 0;

	git_config(git_tar_config);

	switch (argc) {
	case 3:
		strbuf_append_string(&current_path, argv[2]);
		strbuf_append_string(&current_path, "/");
		/* FALLTHROUGH */
	case 2:
		if (get_sha1(argv[1], sha1))
			die("Not a valid object name %s", argv[1]);
		break;
	default:
		usage(tar_tree_usage);
	}

	commit = lookup_commit_reference_gently(sha1, 1);
	if (commit) {
		write_global_extended_header(commit->object.sha1);
		archive_time = commit->date;
	} else
		archive_time = time(NULL);

	tree.buf = buffer = read_object_with_reference(sha1, tree_type,
	                                               &tree.size, tree_sha1);
	if (!tree.buf)
		die("not a reference to a tag, commit or tree object: %s",
		    sha1_to_hex(sha1));

	if (current_path.len > 0)
		write_entry(tree_sha1, &current_path, 040777, NULL, 0);
	traverse_tree(&tree, &current_path);
	write_trailer();
	free(buffer);
	free(current_path.buf);
	return 0;
}

static const char *exec = "git-upload-tar";

static int remote_tar(int argc, const char **argv)
{
	int fd[2], ret, len;
	pid_t pid;
	char buf[1024];
	char *url;

	if (argc < 3 || 4 < argc)
		usage(tar_tree_usage);

	/* --remote=<repo> */
	url = strdup(argv[1]+9);
	pid = git_connect(fd, url, exec);
	if (pid < 0)
		return 1;

	packet_write(fd[1], "want %s\n", argv[2]);
	if (argv[3])
		packet_write(fd[1], "base %s\n", argv[3]);
	packet_flush(fd[1]);

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (!len)
		die("git-tar-tree: expected ACK/NAK, got EOF");
	if (buf[len-1] == '\n')
		buf[--len] = 0;
	if (strcmp(buf, "ACK")) {
		if (5 < len && !strncmp(buf, "NACK ", 5))
			die("git-tar-tree: NACK %s", buf + 5);
		die("git-tar-tree: protocol error");
	}
	/* expect a flush */
	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (len)
		die("git-tar-tree: expected a flush");

	/* Now, start reading from fd[0] and spit it out to stdout */
	ret = copy_fd(fd[0], 1);
	close(fd[0]);

	ret |= finish_connect(pid);
	return !!ret;
}

int cmd_tar_tree(int argc, const char **argv, const char *prefix)
{
	if (argc < 2)
		usage(tar_tree_usage);
	if (!strncmp("--remote=", argv[1], 9))
		return remote_tar(argc, argv);
	return generate_tar(argc, argv, prefix);
}

/* ustar header + extended global header content */
#define HEADERSIZE (2 * RECORDSIZE)

int cmd_get_tar_commit_id(int argc, const char **argv, const char *prefix)
{
	char buffer[HEADERSIZE];
	struct ustar_header *header = (struct ustar_header *)buffer;
	char *content = buffer + RECORDSIZE;
	ssize_t n;

	n = xread(0, buffer, HEADERSIZE);
	if (n < HEADERSIZE)
		die("git-get-tar-commit-id: read error");
	if (header->typeflag[0] != 'g')
		return 1;
	if (memcmp(content, "52 comment=", 11))
		return 1;

	n = xwrite(1, content + 11, 41);
	if (n < 41)
		die("git-get-tar-commit-id: write error");

	return 0;
}
