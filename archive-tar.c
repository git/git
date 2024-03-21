/*
 * Copyright (c) 2005, 2006 Rene Scharfe
 */
#include "git-compat-util.h"
#include "config.h"
#include "gettext.h"
#include "git-zlib.h"
#include "hex.h"
#include "tar.h"
#include "archive.h"
#include "object-store-ll.h"
#include "strbuf.h"
#include "streaming.h"
#include "run-command.h"
#include "write-or-die.h"

#define RECORDSIZE	(512)
#define BLOCKSIZE	(RECORDSIZE * 20)

static char block[BLOCKSIZE];
static unsigned long offset;

static int tar_umask = 002;

static int write_tar_filter_archive(const struct archiver *ar,
				    struct archiver_args *args);

/*
 * This is the max value that a ustar size header can specify, as it is fixed
 * at 11 octal digits. POSIX specifies that we switch to extended headers at
 * this size.
 *
 * Likewise for the mtime (which happens to use a buffer of the same size).
 */
#if ULONG_MAX == 0xFFFFFFFF
#define USTAR_MAX_SIZE ULONG_MAX
#else
#define USTAR_MAX_SIZE 077777777777UL
#endif
#if TIME_MAX == 0xFFFFFFFF
#define USTAR_MAX_MTIME TIME_MAX
#else
#define USTAR_MAX_MTIME 077777777777ULL
#endif

static void tar_write_block(const void *buf)
{
	write_or_die(1, buf, BLOCKSIZE);
}

static void (*write_block)(const void *) = tar_write_block;

/* writes out the whole block, but only if it is full */
static void write_if_needed(void)
{
	if (offset == BLOCKSIZE) {
		write_block(block);
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
		write_block(buf);
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
	write_block(block);
	if (tail < 2 * RECORDSIZE) {
		memset(block, 0, offset);
		write_block(block);
	}
}

/*
 * queues up writes, so that all our write(2) calls write exactly one
 * full block; pads writes to RECORDSIZE
 */
static int stream_blocked(struct repository *r, const struct object_id *oid)
{
	struct git_istream *st;
	enum object_type type;
	unsigned long sz;
	char buf[BLOCKSIZE];
	ssize_t readlen;

	st = open_istream(r, oid, &type, &sz, NULL);
	if (!st)
		return error(_("cannot stream blob %s"), oid_to_hex(oid));
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
				     const char *value, size_t valuelen)
{
	size_t orig_len = sb->len;
	size_t len, tmp;

	/* "%u %s=%s\n" */
	len = 1 + 1 + strlen(keyword) + 1 + valuelen + 1;
	for (tmp = 1; len / 10 >= tmp; tmp *= 10)
		len++;

	strbuf_grow(sb, len);
	strbuf_addf(sb, "%"PRIuMAX" %s=", (uintmax_t)len, keyword);
	strbuf_add(sb, value, valuelen);
	strbuf_addch(sb, '\n');

	if (len != sb->len - orig_len)
		BUG("pax extended header length miscalculated as %"PRIuMAX
		    ", should be %"PRIuMAX,
		    (uintmax_t)len, (uintmax_t)(sb->len - orig_len));
}

/*
 * Like strbuf_append_ext_header, but for numeric values.
 */
static void strbuf_append_ext_header_uint(struct strbuf *sb,
					  const char *keyword,
					  uintmax_t value)
{
	char buf[40]; /* big enough for 2^128 in decimal, plus NUL */
	int len;

	len = xsnprintf(buf, sizeof(buf), "%"PRIuMAX, value);
	strbuf_append_ext_header(sb, keyword, buf, len);
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
	xsnprintf(header->mode, sizeof(header->mode), "%07o", mode & 07777);
	xsnprintf(header->size, sizeof(header->size), "%011"PRIoMAX , S_ISREG(mode) ? (uintmax_t)size : (uintmax_t)0);
	xsnprintf(header->mtime, sizeof(header->mtime), "%011lo", (unsigned long) args->time);

	xsnprintf(header->uid, sizeof(header->uid), "%07o", 0);
	xsnprintf(header->gid, sizeof(header->gid), "%07o", 0);
	strlcpy(header->uname, "root", sizeof(header->uname));
	strlcpy(header->gname, "root", sizeof(header->gname));
	xsnprintf(header->devmajor, sizeof(header->devmajor), "%07o", 0);
	xsnprintf(header->devminor, sizeof(header->devminor), "%07o", 0);

	memcpy(header->magic, "ustar", 6);
	memcpy(header->version, "00", 2);

	xsnprintf(header->chksum, sizeof(header->chksum), "%07o", ustar_header_chksum(header));
}

static void write_extended_header(struct archiver_args *args,
				  const struct object_id *oid,
				  const void *buffer, unsigned long size)
{
	struct ustar_header header;
	unsigned int mode;
	memset(&header, 0, sizeof(header));
	*header.typeflag = TYPEFLAG_EXT_HEADER;
	mode = 0100666;
	xsnprintf(header.name, sizeof(header.name), "%s.paxheader", oid_to_hex(oid));
	prepare_header(args, &header, mode, size);
	write_blocked(&header, sizeof(header));
	write_blocked(buffer, size);
}

static int write_tar_entry(struct archiver_args *args,
			   const struct object_id *oid,
			   const char *path, size_t pathlen,
			   unsigned int mode,
			   void *buffer, unsigned long size)
{
	struct ustar_header header;
	struct strbuf ext_header = STRBUF_INIT;
	unsigned long size_in_header;
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
		return error(_("unsupported file mode: 0%o (SHA1: %s)"),
			     mode, oid_to_hex(oid));
	}
	if (pathlen > sizeof(header.name)) {
		size_t plen = get_path_prefix(path, pathlen,
					      sizeof(header.prefix));
		size_t rest = pathlen - plen - 1;
		if (plen > 0 && rest <= sizeof(header.name)) {
			memcpy(header.prefix, path, plen);
			memcpy(header.name, path + plen + 1, rest);
		} else {
			xsnprintf(header.name, sizeof(header.name), "%s.data",
				  oid_to_hex(oid));
			strbuf_append_ext_header(&ext_header, "path",
						 path, pathlen);
		}
	} else
		memcpy(header.name, path, pathlen);

	if (S_ISLNK(mode)) {
		if (size > sizeof(header.linkname)) {
			xsnprintf(header.linkname, sizeof(header.linkname),
				  "see %s.paxheader", oid_to_hex(oid));
			strbuf_append_ext_header(&ext_header, "linkpath",
			                         buffer, size);
		} else
			memcpy(header.linkname, buffer, size);
	}

	size_in_header = size;
	if (S_ISREG(mode) && size > USTAR_MAX_SIZE) {
		size_in_header = 0;
		strbuf_append_ext_header_uint(&ext_header, "size", size);
	}

	prepare_header(args, &header, mode, size_in_header);

	if (ext_header.len > 0) {
		write_extended_header(args, oid, ext_header.buf,
				      ext_header.len);
	}
	strbuf_release(&ext_header);
	write_blocked(&header, sizeof(header));
	if (S_ISREG(mode) && size > 0) {
		if (buffer)
			write_blocked(buffer, size);
		else
			err = stream_blocked(args->repo, oid);
	}
	return err;
}

static void write_global_extended_header(struct archiver_args *args)
{
	const struct object_id *oid = args->commit_oid;
	struct strbuf ext_header = STRBUF_INIT;
	struct ustar_header header;
	unsigned int mode;

	if (oid)
		strbuf_append_ext_header(&ext_header, "comment",
					 oid_to_hex(oid),
					 the_hash_algo->hexsz);
	if (args->time > USTAR_MAX_MTIME) {
		strbuf_append_ext_header_uint(&ext_header, "mtime",
					      args->time);
		args->time = USTAR_MAX_MTIME;
	}

	if (!ext_header.len)
		return;

	memset(&header, 0, sizeof(header));
	*header.typeflag = TYPEFLAG_GLOBAL_HEADER;
	mode = 0100666;
	xsnprintf(header.name, sizeof(header.name), "pax_global_header");
	prepare_header(args, &header, mode, ext_header.len);
	write_blocked(&header, sizeof(header));
	write_blocked(ext_header.buf, ext_header.len);
	strbuf_release(&ext_header);
}

static struct archiver **tar_filters;
static int nr_tar_filters;
static int alloc_tar_filters;

static struct archiver *find_tar_filter(const char *name, size_t len)
{
	int i;
	for (i = 0; i < nr_tar_filters; i++) {
		struct archiver *ar = tar_filters[i];
		if (!xstrncmpz(ar->name, name, len))
			return ar;
	}
	return NULL;
}

static int tar_filter_config(const char *var, const char *value,
			     void *data UNUSED)
{
	struct archiver *ar;
	const char *name;
	const char *type;
	size_t namelen;

	if (parse_config_key(var, "tar", &name, &namelen, &type) < 0 || !name)
		return 0;

	ar = find_tar_filter(name, namelen);
	if (!ar) {
		CALLOC_ARRAY(ar, 1);
		ar->name = xmemdupz(name, namelen);
		ar->write_archive = write_tar_filter_archive;
		ar->flags = ARCHIVER_WANT_COMPRESSION_LEVELS |
			    ARCHIVER_HIGH_COMPRESSION_LEVELS;
		ALLOC_GROW(tar_filters, nr_tar_filters + 1, alloc_tar_filters);
		tar_filters[nr_tar_filters++] = ar;
	}

	if (!strcmp(type, "command")) {
		if (!value)
			return config_error_nonbool(var);
		free(ar->filter_command);
		ar->filter_command = xstrdup(value);
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

static int git_tar_config(const char *var, const char *value,
			  const struct config_context *ctx, void *cb)
{
	if (!strcmp(var, "tar.umask")) {
		if (value && !strcmp(value, "user")) {
			tar_umask = umask(0);
			umask(tar_umask);
		} else {
			tar_umask = git_config_int(var, value, ctx->kvi);
		}
		return 0;
	}

	return tar_filter_config(var, value, cb);
}

static int write_tar_archive(const struct archiver *ar UNUSED,
			     struct archiver_args *args)
{
	int err = 0;

	write_global_extended_header(args);
	err = write_archive_entries(args, write_tar_entry);
	if (!err)
		write_trailer();
	return err;
}

static git_zstream gzstream;
static unsigned char outbuf[16384];

static void tgz_deflate(int flush)
{
	while (gzstream.avail_in || flush == Z_FINISH) {
		int status = git_deflate(&gzstream, flush);
		if (!gzstream.avail_out || status == Z_STREAM_END) {
			write_or_die(1, outbuf, gzstream.next_out - outbuf);
			gzstream.next_out = outbuf;
			gzstream.avail_out = sizeof(outbuf);
			if (status == Z_STREAM_END)
				break;
		}
		if (status != Z_OK && status != Z_BUF_ERROR)
			die(_("deflate error (%d)"), status);
	}
}

static void tgz_write_block(const void *data)
{
	gzstream.next_in = (void *)data;
	gzstream.avail_in = BLOCKSIZE;
	tgz_deflate(Z_NO_FLUSH);
}

static const char internal_gzip_command[] = "git archive gzip";

static int write_tar_filter_archive(const struct archiver *ar,
				    struct archiver_args *args)
{
#if ZLIB_VERNUM >= 0x1221
	struct gz_header_s gzhead = { .os = 3 }; /* Unix, for reproducibility */
#endif
	struct strbuf cmd = STRBUF_INIT;
	struct child_process filter = CHILD_PROCESS_INIT;
	int r;

	if (!ar->filter_command)
		BUG("tar-filter archiver called with no filter defined");

	if (!strcmp(ar->filter_command, internal_gzip_command)) {
		write_block = tgz_write_block;
		git_deflate_init_gzip(&gzstream, args->compression_level);
#if ZLIB_VERNUM >= 0x1221
		if (deflateSetHeader(&gzstream.z, &gzhead) != Z_OK)
			BUG("deflateSetHeader() called too late");
#endif
		gzstream.next_out = outbuf;
		gzstream.avail_out = sizeof(outbuf);

		r = write_tar_archive(ar, args);

		tgz_deflate(Z_FINISH);
		git_deflate_end(&gzstream);
		return r;
	}

	strbuf_addstr(&cmd, ar->filter_command);
	if (args->compression_level >= 0)
		strbuf_addf(&cmd, " -%d", args->compression_level);

	strvec_push(&filter.args, cmd.buf);
	filter.use_shell = 1;
	filter.in = -1;
	filter.silent_exec_failure = 1;

	if (start_command(&filter) < 0)
		die_errno(_("unable to start '%s' filter"), cmd.buf);
	close(1);
	if (dup2(filter.in, 1) < 0)
		die_errno(_("unable to redirect descriptor"));
	close(filter.in);

	r = write_tar_archive(ar, args);

	close(1);
	if (finish_command(&filter) != 0)
		die(_("'%s' filter reported error"), cmd.buf);

	strbuf_release(&cmd);
	return r;
}

static struct archiver tar_archiver = {
	.name = "tar",
	.write_archive = write_tar_archive,
	.flags = ARCHIVER_REMOTE,
};

void init_tar_archiver(void)
{
	int i;
	register_archiver(&tar_archiver);

	tar_filter_config("tar.tgz.command", internal_gzip_command, NULL);
	tar_filter_config("tar.tgz.remote", "true", NULL);
	tar_filter_config("tar.tar.gz.command", internal_gzip_command, NULL);
	tar_filter_config("tar.tar.gz.remote", "true", NULL);
	git_config(git_tar_config, NULL);
	for (i = 0; i < nr_tar_filters; i++) {
		/* omit any filters that never had a command configured */
		if (tar_filters[i]->filter_command)
			register_archiver(tar_filters[i]);
	}
}
