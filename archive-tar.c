/*
 * Copyright (c) 2005, 2006 Rene Scharfe
 */
#include "cache.h"
#include "tar.h"
#include "archive.h"
#include "streaming.h"
#include "run-command.h"

#define RECORDSIZE	(512)
#define BLOCKSIZE	(RECORDSIZE * 20)

static char block[BLOCKSIZE];
static unsigned long offset;

static int tar_umask = 002;

static int write_tar_filter_archive(const struct archiver *ar,
				    struct archiver_args *args);

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
static void do_write_blocked(const void *data, unsigned long size)
{
	const char *buf = data;

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
}

static void finish_record(void)
{
	unsigned long tail;
	tail = offset % RECORDSIZE;
	if (tail)  {
		memset(block + offset, 0, RECORDSIZE - tail);
		offset += RECORDSIZE - tail;
	}
	write_if_needed();
}

static void write_blocked(const void *data, unsigned long size)
{
	do_write_blocked(data, size);
	finish_record();
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

/*
 * queues up writes, so that all our write(2) calls write exactly one
 * full block; pads writes to RECORDSIZE
 */
static int stream_blocked(const unsigned char *sha1)
{
	struct git_istream *st;
	enum object_type type;
	unsigned long sz;
	char buf[BLOCKSIZE];
	ssize_t readlen;

	st = open_istream(sha1, &type, &sz, NULL);
	if (!st)
		return error("cannot stream blob %s", sha1_to_hex(sha1));
	for (;;) {
		readlen = read_istream(st, buf, sizeof(buf));
		if (readlen <= 0)
			break;
		do_write_blocked(buf, readlen);
	}
	close_istream(st);
	if (!readlen)
		finish_record();
	return readlen;
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
	int len, tmp;

	/* "%u %s=%s\n" */
	len = 1 + 1 + strlen(keyword) + 1 + valuelen + 1;
	for (tmp = len; tmp > 9; tmp /= 10)
		len++;

	strbuf_grow(sb, len);
	strbuf_addf(sb, "%u %s=", len, keyword);
	strbuf_add(sb, value, valuelen);
	strbuf_addch(sb, '\n');
}

static unsigned int ustar_header_chksum(const struct ustar_header *header)
{
	const unsigned char *p = (const unsigned char *)header;
	unsigned int chksum = 0;
	while (p < (const unsigned char *)header->chksum)
		chksum += *p++;
	chksum += sizeof(header->chksum) * ' ';
	p += sizeof(header->chksum);
	while (p < (const unsigned char *)header + sizeof(struct ustar_header))
		chksum += *p++;
	return chksum;
}

static size_t get_path_prefix(const char *path, size_t pathlen, size_t maxlen)
{
	size_t i = pathlen;
	if (i > 1 && path[i - 1] == '/')
		i--;
	if (i > maxlen)
		i = maxlen;
	do {
		i--;
	} while (i > 0 && path[i] != '/');
	return i;
}

static void prepare_header(struct archiver_args *args,
			   struct ustar_header *header,
			   unsigned int mode, unsigned long size)
{
	sprintf(header->mode, "%07o", mode & 07777);
	sprintf(header->size, "%011lo", S_ISREG(mode) ? size : 0);
	sprintf(header->mtime, "%011lo", (unsigned long) args->time);

	sprintf(header->uid, "%07o", 0);
	sprintf(header->gid, "%07o", 0);
	strlcpy(header->uname, "root", sizeof(header->uname));
	strlcpy(header->gname, "root", sizeof(header->gname));
	sprintf(header->devmajor, "%07o", 0);
	sprintf(header->devminor, "%07o", 0);

	memcpy(header->magic, "ustar", 6);
	memcpy(header->version, "00", 2);

	sprintf(header->chksum, "%07o", ustar_header_chksum(header));
}

static int write_extended_header(struct archiver_args *args,
				 const unsigned char *sha1,
				 const void *buffer, unsigned long size)
{
	struct ustar_header header;
	unsigned int mode;
	memset(&header, 0, sizeof(header));
	*header.typeflag = TYPEFLAG_EXT_HEADER;
	mode = 0100666;
	sprintf(header.name, "%s.paxheader", sha1_to_hex(sha1));
	prepare_header(args, &header, mode, size);
	write_blocked(&header, sizeof(header));
	write_blocked(buffer, size);
	return 0;
}

static int write_tar_entry(struct archiver_args *args,
			   const unsigned char *sha1,
			   const char *path, size_t pathlen,
			   unsigned int mode)
{
	struct ustar_header header;
	struct strbuf ext_header = STRBUF_INIT;
	unsigned int old_mode = mode;
	unsigned long size;
	void *buffer;
	int err = 0;

	memset(&header, 0, sizeof(header));

	if (S_ISDIR(mode) || S_ISGITLINK(mode)) {
		*header.typeflag = TYPEFLAG_DIR;
		mode = (mode | 0777) & ~tar_umask;
	} else if (S_ISLNK(mode)) {
		*header.typeflag = TYPEFLAG_LNK;
		mode |= 0777;
	} else if (S_ISREG(mode)) {
		*header.typeflag = TYPEFLAG_REG;
		mode = (mode | ((mode & 0100) ? 0777 : 0666)) & ~tar_umask;
	} else {
		return error("unsupported file mode: 0%o (SHA1: %s)",
			     mode, sha1_to_hex(sha1));
	}
	if (pathlen > sizeof(header.name)) {
		size_t plen = get_path_prefix(path, pathlen,
					      sizeof(header.prefix));
		size_t rest = pathlen - plen - 1;
		if (plen > 0 && rest <= sizeof(header.name)) {
			memcpy(header.prefix, path, plen);
				memcpy(header.name, path + plen + 1, rest);
		} else {
			sprintf(header.name, "%s.data",
				sha1_to_hex(sha1));
			strbuf_append_ext_header(&ext_header, "path",
						 path, pathlen);
		}
	} else
		memcpy(header.name, path, pathlen);

	if (S_ISREG(mode) && !args->convert &&
	    sha1_object_info(sha1, &size) == OBJ_BLOB &&
	    size > big_file_threshold)
		buffer = NULL;
	else if (S_ISLNK(mode) || S_ISREG(mode)) {
		enum object_type type;
		buffer = sha1_file_to_archive(args, path, sha1, old_mode, &type, &size);
		if (!buffer)
			return error("cannot read %s", sha1_to_hex(sha1));
	} else {
		buffer = NULL;
		size = 0;
	}

	if (S_ISLNK(mode)) {
		if (size > sizeof(header.linkname)) {
			sprintf(header.linkname, "see %s.paxheader",
			        sha1_to_hex(sha1));
			strbuf_append_ext_header(&ext_header, "linkpath",
			                         buffer, size);
		} else
			memcpy(header.linkname, buffer, size);
	}

	prepare_header(args, &header, mode, size);

	if (ext_header.len > 0) {
		err = write_extended_header(args, sha1, ext_header.buf,
					    ext_header.len);
		if (err) {
			free(buffer);
			return err;
		}
	}
	strbuf_release(&ext_header);
	write_blocked(&header, sizeof(header));
	if (S_ISREG(mode) && size > 0) {
		if (buffer)
			write_blocked(buffer, size);
		else
			err = stream_blocked(sha1);
	}
	free(buffer);
	return err;
}

static int write_global_extended_header(struct archiver_args *args)
{
	const unsigned char *sha1 = args->commit_sha1;
	struct strbuf ext_header = STRBUF_INIT;
	struct ustar_header header;
	unsigned int mode;
	int err = 0;

	strbuf_append_ext_header(&ext_header, "comment", sha1_to_hex(sha1), 40);
	memset(&header, 0, sizeof(header));
	*header.typeflag = TYPEFLAG_GLOBAL_HEADER;
	mode = 0100666;
	strcpy(header.name, "pax_global_header");
	prepare_header(args, &header, mode, ext_header.len);
	write_blocked(&header, sizeof(header));
	write_blocked(ext_header.buf, ext_header.len);
	strbuf_release(&ext_header);
	return err;
}

static struct archiver **tar_filters;
static int nr_tar_filters;
static int alloc_tar_filters;

static struct archiver *find_tar_filter(const char *name, int len)
{
	int i;
	for (i = 0; i < nr_tar_filters; i++) {
		struct archiver *ar = tar_filters[i];
		if (!strncmp(ar->name, name, len) && !ar->name[len])
			return ar;
	}
	return NULL;
}

static int tar_filter_config(const char *var, const char *value, void *data)
{
	struct archiver *ar;
	const char *dot;
	const char *name;
	const char *type;
	int namelen;

	if (prefixcmp(var, "tar."))
		return 0;
	dot = strrchr(var, '.');
	if (dot == var + 9)
		return 0;

	name = var + 4;
	namelen = dot - name;
	type = dot + 1;

	ar = find_tar_filter(name, namelen);
	if (!ar) {
		ar = xcalloc(1, sizeof(*ar));
		ar->name = xmemdupz(name, namelen);
		ar->write_archive = write_tar_filter_archive;
		ar->flags = ARCHIVER_WANT_COMPRESSION_LEVELS;
		ALLOC_GROW(tar_filters, nr_tar_filters + 1, alloc_tar_filters);
		tar_filters[nr_tar_filters++] = ar;
	}

	if (!strcmp(type, "command")) {
		if (!value)
			return config_error_nonbool(var);
		free(ar->data);
		ar->data = xstrdup(value);
		return 0;
	}
	if (!strcmp(type, "remote")) {
		if (git_config_bool(var, value))
			ar->flags |= ARCHIVER_REMOTE;
		else
			ar->flags &= ~ARCHIVER_REMOTE;
		return 0;
	}

	return 0;
}

static int git_tar_config(const char *var, const char *value, void *cb)
{
	if (!strcmp(var, "tar.umask")) {
		if (value && !strcmp(value, "user")) {
			tar_umask = umask(0);
			umask(tar_umask);
		} else {
			tar_umask = git_config_int(var, value);
		}
		return 0;
	}

	return tar_filter_config(var, value, cb);
}

static int write_tar_archive(const struct archiver *ar,
			     struct archiver_args *args)
{
	int err = 0;

	if (args->commit_sha1)
		err = write_global_extended_header(args);
	if (!err)
		err = write_archive_entries(args, write_tar_entry);
	if (!err)
		write_trailer();
	return err;
}

static int write_tar_filter_archive(const struct archiver *ar,
				    struct archiver_args *args)
{
	struct strbuf cmd = STRBUF_INIT;
	struct child_process filter;
	const char *argv[2];
	int r;

	if (!ar->data)
		die("BUG: tar-filter archiver called with no filter defined");

	strbuf_addstr(&cmd, ar->data);
	if (args->compression_level >= 0)
		strbuf_addf(&cmd, " -%d", args->compression_level);

	memset(&filter, 0, sizeof(filter));
	argv[0] = cmd.buf;
	argv[1] = NULL;
	filter.argv = argv;
	filter.use_shell = 1;
	filter.in = -1;

	if (start_command(&filter) < 0)
		die_errno("unable to start '%s' filter", argv[0]);
	close(1);
	if (dup2(filter.in, 1) < 0)
		die_errno("unable to redirect descriptor");
	close(filter.in);

	r = write_tar_archive(ar, args);

	close(1);
	if (finish_command(&filter) != 0)
		die("'%s' filter reported error", argv[0]);

	strbuf_release(&cmd);
	return r;
}

static struct archiver tar_archiver = {
	"tar",
	write_tar_archive,
	ARCHIVER_REMOTE
};

void init_tar_archiver(void)
{
	int i;
	register_archiver(&tar_archiver);

	tar_filter_config("tar.tgz.command", "gzip -cn", NULL);
	tar_filter_config("tar.tgz.remote", "true", NULL);
	tar_filter_config("tar.tar.gz.command", "gzip -cn", NULL);
	tar_filter_config("tar.tar.gz.remote", "true", NULL);
	git_config(git_tar_config, NULL);
	for (i = 0; i < nr_tar_filters; i++) {
		/* omit any filters that never had a command configured */
		if (tar_filters[i]->data)
			register_archiver(tar_filters[i]);
	}
}
