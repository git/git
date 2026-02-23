/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 *
 * This handles basic git object files - packing, unpacking,
 * creation etc.
 */

#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "convert.h"
#include "dir.h"
#include "environment.h"
#include "fsck.h"
#include "gettext.h"
#include "hex.h"
#include "loose.h"
#include "object-file-convert.h"
#include "object-file.h"
#include "odb.h"
#include "odb/streaming.h"
#include "oidtree.h"
#include "pack.h"
#include "packfile.h"
#include "path.h"
#include "read-cache-ll.h"
#include "setup.h"
#include "tempfile.h"
#include "tmp-objdir.h"

/* The maximum size for an object header. */
#define MAX_HEADER_LEN 32

static int get_conv_flags(unsigned flags)
{
	if (flags & INDEX_RENORMALIZE)
		return CONV_EOL_RENORMALIZE;
	else if (flags & INDEX_WRITE_OBJECT)
		return global_conv_flags_eol | CONV_WRITE_OBJECT;
	else
		return 0;
}

static void fill_loose_path(struct strbuf *buf,
			    const struct object_id *oid,
			    const struct git_hash_algo *algop)
{
	for (size_t i = 0; i < algop->rawsz; i++) {
		static char hex[] = "0123456789abcdef";
		unsigned int val = oid->hash[i];
		strbuf_addch(buf, hex[val >> 4]);
		strbuf_addch(buf, hex[val & 0xf]);
		if (!i)
			strbuf_addch(buf, '/');
	}
}

const char *odb_loose_path(struct odb_source *source,
			   struct strbuf *buf,
			   const struct object_id *oid)
{
	strbuf_reset(buf);
	strbuf_addstr(buf, source->path);
	strbuf_addch(buf, '/');
	fill_loose_path(buf, oid, source->odb->repo->hash_algo);
	return buf->buf;
}

/* Returns 1 if we have successfully freshened the file, 0 otherwise. */
static int freshen_file(const char *fn)
{
	return !utime(fn, NULL);
}

/*
 * All of the check_and_freshen functions return 1 if the file exists and was
 * freshened (if freshening was requested), 0 otherwise. If they return
 * 0, you should not assume that it is safe to skip a write of the object (it
 * either does not exist on disk, or has a stale mtime and may be subject to
 * pruning).
 */
int check_and_freshen_file(const char *fn, int freshen)
{
	if (access(fn, F_OK))
		return 0;
	if (freshen && !freshen_file(fn))
		return 0;
	return 1;
}

static int check_and_freshen_source(struct odb_source *source,
				    const struct object_id *oid,
				    int freshen)
{
	static struct strbuf path = STRBUF_INIT;
	odb_loose_path(source, &path, oid);
	return check_and_freshen_file(path.buf, freshen);
}

int odb_source_loose_has_object(struct odb_source *source,
				const struct object_id *oid)
{
	return check_and_freshen_source(source, oid, 0);
}

int format_object_header(char *str, size_t size, enum object_type type,
			 size_t objsize)
{
	const char *name = type_name(type);

	if (!name)
		BUG("could not get a type name for 'enum object_type' value %d", type);

	return xsnprintf(str, size, "%s %"PRIuMAX, name, (uintmax_t)objsize) + 1;
}

int check_object_signature(struct repository *r, const struct object_id *oid,
			   void *buf, unsigned long size,
			   enum object_type type)
{
	const struct git_hash_algo *algo =
		oid->algo ? &hash_algos[oid->algo] : r->hash_algo;
	struct object_id real_oid;

	hash_object_file(algo, buf, size, type, &real_oid);

	return !oideq(oid, &real_oid) ? -1 : 0;
}

int stream_object_signature(struct repository *r, const struct object_id *oid)
{
	struct object_id real_oid;
	struct odb_read_stream *st;
	struct git_hash_ctx c;
	char hdr[MAX_HEADER_LEN];
	int hdrlen;

	st = odb_read_stream_open(r->objects, oid, NULL);
	if (!st)
		return -1;

	/* Generate the header */
	hdrlen = format_object_header(hdr, sizeof(hdr), st->type, st->size);

	/* Sha1.. */
	r->hash_algo->init_fn(&c);
	git_hash_update(&c, hdr, hdrlen);
	for (;;) {
		char buf[1024 * 16];
		ssize_t readlen = odb_read_stream_read(st, buf, sizeof(buf));

		if (readlen < 0) {
			odb_read_stream_close(st);
			return -1;
		}
		if (!readlen)
			break;
		git_hash_update(&c, buf, readlen);
	}
	git_hash_final_oid(&real_oid, &c);
	odb_read_stream_close(st);
	return !oideq(oid, &real_oid) ? -1 : 0;
}

/*
 * Find "oid" as a loose object in given source, open the object and return its
 * file descriptor. Returns the file descriptor on success, negative on failure.
 *
 * The "path" out-parameter will give the path of the object we found (if any).
 * Note that it may point to static storage and is only valid until another
 * call to stat_loose_object().
 */
static int open_loose_object(struct odb_source_loose *loose,
			     const struct object_id *oid, const char **path)
{
	static struct strbuf buf = STRBUF_INIT;
	int fd;

	*path = odb_loose_path(loose->source, &buf, oid);
	fd = git_open(*path);
	if (fd >= 0)
		return fd;

	return -1;
}

static int quick_has_loose(struct odb_source_loose *loose,
			   const struct object_id *oid)
{
	return !!oidtree_contains(odb_source_loose_cache(loose->source, oid), oid);
}

/*
 * Map and close the given loose object fd. The path argument is used for
 * error reporting.
 */
static void *map_fd(int fd, const char *path, unsigned long *size)
{
	void *map = NULL;
	struct stat st;

	if (!fstat(fd, &st)) {
		*size = xsize_t(st.st_size);
		if (!*size) {
			/* mmap() is forbidden on empty files */
			error(_("object file %s is empty"), path);
			close(fd);
			return NULL;
		}
		map = xmmap(NULL, *size, PROT_READ, MAP_PRIVATE, fd, 0);
	}
	close(fd);
	return map;
}

static void *odb_source_loose_map_object(struct odb_source *source,
					 const struct object_id *oid,
					 unsigned long *size)
{
	const char *p;
	int fd = open_loose_object(source->loose, oid, &p);

	if (fd < 0)
		return NULL;
	return map_fd(fd, p, size);
}

enum unpack_loose_header_result {
	ULHR_OK,
	ULHR_BAD,
	ULHR_TOO_LONG,
};

/**
 * unpack_loose_header() initializes the data stream needed to unpack
 * a loose object header.
 *
 * Returns:
 *
 * - ULHR_OK on success
 * - ULHR_BAD on error
 * - ULHR_TOO_LONG if the header was too long
 *
 * It will only parse up to MAX_HEADER_LEN bytes.
 */
static enum unpack_loose_header_result unpack_loose_header(git_zstream *stream,
							   unsigned char *map,
							   unsigned long mapsize,
							   void *buffer,
							   unsigned long bufsiz)
{
	int status;

	/* Get the data stream */
	memset(stream, 0, sizeof(*stream));
	stream->next_in = map;
	stream->avail_in = mapsize;
	stream->next_out = buffer;
	stream->avail_out = bufsiz;

	git_inflate_init(stream);
	obj_read_unlock();
	status = git_inflate(stream, 0);
	obj_read_lock();
	if (status != Z_OK && status != Z_STREAM_END)
		return ULHR_BAD;

	/*
	 * Check if entire header is unpacked in the first iteration.
	 */
	if (memchr(buffer, '\0', stream->next_out - (unsigned char *)buffer))
		return ULHR_OK;

	/*
	 * We have a header longer than MAX_HEADER_LEN.
	 */
	return ULHR_TOO_LONG;
}

static void *unpack_loose_rest(git_zstream *stream,
			       void *buffer, unsigned long size,
			       const struct object_id *oid)
{
	size_t bytes = strlen(buffer) + 1, n;
	unsigned char *buf = xmallocz(size);
	int status = Z_OK;

	n = stream->total_out - bytes;
	if (n > size)
		n = size;
	memcpy(buf, (char *) buffer + bytes, n);
	bytes = n;
	if (bytes <= size) {
		/*
		 * The above condition must be (bytes <= size), not
		 * (bytes < size).  In other words, even though we
		 * expect no more output and set avail_out to zero,
		 * the input zlib stream may have bytes that express
		 * "this concludes the stream", and we *do* want to
		 * eat that input.
		 *
		 * Otherwise we would not be able to test that we
		 * consumed all the input to reach the expected size;
		 * we also want to check that zlib tells us that all
		 * went well with status == Z_STREAM_END at the end.
		 */
		stream->next_out = buf + bytes;
		stream->avail_out = size - bytes;
		while (status == Z_OK) {
			obj_read_unlock();
			status = git_inflate(stream, Z_FINISH);
			obj_read_lock();
		}
	}

	if (status != Z_STREAM_END) {
		error(_("corrupt loose object '%s'"), oid_to_hex(oid));
		FREE_AND_NULL(buf);
	} else if (stream->avail_in) {
		error(_("garbage at end of loose object '%s'"),
		      oid_to_hex(oid));
		FREE_AND_NULL(buf);
	}

	return buf;
}

/*
 * parse_loose_header() parses the starting "<type> <len>\0" of an
 * object. If it doesn't follow that format -1 is returned. To check
 * the validity of the <type> populate the "typep" in the "struct
 * object_info". It will be OBJ_BAD if the object type is unknown. The
 * parsed <len> can be retrieved via "oi->sizep", and from there
 * passed to unpack_loose_rest().
 *
 * We used to just use "sscanf()", but that's actually way
 * too permissive for what we want to check. So do an anal
 * object header parse by hand.
 */
static int parse_loose_header(const char *hdr, struct object_info *oi)
{
	const char *type_buf = hdr;
	size_t size;
	int type, type_len = 0;

	/*
	 * The type can be of any size but is followed by
	 * a space.
	 */
	for (;;) {
		char c = *hdr++;
		if (!c)
			return -1;
		if (c == ' ')
			break;
		type_len++;
	}

	type = type_from_string_gently(type_buf, type_len, 1);
	if (oi->typep)
		*oi->typep = type;

	/*
	 * The length must follow immediately, and be in canonical
	 * decimal format (ie "010" is not valid).
	 */
	size = *hdr++ - '0';
	if (size > 9)
		return -1;
	if (size) {
		for (;;) {
			unsigned long c = *hdr - '0';
			if (c > 9)
				break;
			hdr++;
			size = st_add(st_mult(size, 10), c);
		}
	}

	if (oi->sizep)
		*oi->sizep = cast_size_t_to_ulong(size);

	/*
	 * The length must be followed by a zero byte
	 */
	if (*hdr)
		return -1;

	/*
	 * The format is valid, but the type may still be bogus. The
	 * Caller needs to check its oi->typep.
	 */
	return 0;
}

static int read_object_info_from_path(struct odb_source *source,
				      const char *path,
				      const struct object_id *oid,
				      struct object_info *oi,
				      enum object_info_flags flags)
{
	int ret;
	int fd;
	unsigned long mapsize;
	void *map = NULL;
	git_zstream stream, *stream_to_end = NULL;
	char hdr[MAX_HEADER_LEN];
	unsigned long size_scratch;
	enum object_type type_scratch;
	struct stat st;

	/*
	 * If we don't care about type or size, then we don't
	 * need to look inside the object at all. Note that we
	 * do not optimize out the stat call, even if the
	 * caller doesn't care about the disk-size, since our
	 * return value implicitly indicates whether the
	 * object even exists.
	 */
	if (!oi || (!oi->typep && !oi->sizep && !oi->contentp)) {
		struct stat st;

		if ((!oi || (!oi->disk_sizep && !oi->mtimep)) && (flags & OBJECT_INFO_QUICK)) {
			ret = quick_has_loose(source->loose, oid) ? 0 : -1;
			goto out;
		}

		if (lstat(path, &st) < 0) {
			ret = -1;
			goto out;
		}

		if (oi) {
			if (oi->disk_sizep)
				*oi->disk_sizep = st.st_size;
			if (oi->mtimep)
				*oi->mtimep = st.st_mtime;
		}

		ret = 0;
		goto out;
	}

	fd = git_open(path);
	if (fd < 0) {
		if (errno != ENOENT)
			error_errno(_("unable to open loose object %s"), oid_to_hex(oid));
		ret = -1;
		goto out;
	}

	if (fstat(fd, &st)) {
		close(fd);
		ret = -1;
		goto out;
	}

	mapsize = xsize_t(st.st_size);
	if (!mapsize) {
		close(fd);
		ret = error(_("object file %s is empty"), path);
		goto out;
	}

	map = xmmap(NULL, mapsize, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (!map) {
		ret = -1;
		goto out;
	}

	if (oi->disk_sizep)
		*oi->disk_sizep = mapsize;
	if (oi->mtimep)
		*oi->mtimep = st.st_mtime;

	stream_to_end = &stream;

	switch (unpack_loose_header(&stream, map, mapsize, hdr, sizeof(hdr))) {
	case ULHR_OK:
		if (!oi->sizep)
			oi->sizep = &size_scratch;
		if (!oi->typep)
			oi->typep = &type_scratch;

		if (parse_loose_header(hdr, oi) < 0) {
			ret = error(_("unable to parse %s header"), oid_to_hex(oid));
			goto corrupt;
		}

		if (*oi->typep < 0)
			die(_("invalid object type"));

		if (oi->contentp) {
			*oi->contentp = unpack_loose_rest(&stream, hdr, *oi->sizep, oid);
			if (!*oi->contentp) {
				ret = -1;
				goto corrupt;
			}
		}

		break;
	case ULHR_BAD:
		ret = error(_("unable to unpack %s header"),
			    oid_to_hex(oid));
		goto corrupt;
	case ULHR_TOO_LONG:
		ret = error(_("header for %s too long, exceeds %d bytes"),
			    oid_to_hex(oid), MAX_HEADER_LEN);
		goto corrupt;
	}

	ret = 0;

corrupt:
	if (ret && (flags & OBJECT_INFO_DIE_IF_CORRUPT))
		die(_("loose object %s (stored in %s) is corrupt"),
		    oid_to_hex(oid), path);

out:
	if (stream_to_end)
		git_inflate_end(stream_to_end);
	if (map)
		munmap(map, mapsize);
	if (oi) {
		if (oi->sizep == &size_scratch)
			oi->sizep = NULL;
		if (oi->typep == &type_scratch)
			oi->typep = NULL;
		if (oi->delta_base_oid)
			oidclr(oi->delta_base_oid, source->odb->repo->hash_algo);
		if (!ret)
			oi->whence = OI_LOOSE;
	}

	return ret;
}

int odb_source_loose_read_object_info(struct odb_source *source,
				      const struct object_id *oid,
				      struct object_info *oi,
				      enum object_info_flags flags)
{
	static struct strbuf buf = STRBUF_INIT;
	odb_loose_path(source, &buf, oid);
	return read_object_info_from_path(source, buf.buf, oid, oi, flags);
}

static void hash_object_body(const struct git_hash_algo *algo, struct git_hash_ctx *c,
			     const void *buf, unsigned long len,
			     struct object_id *oid,
			     char *hdr, int *hdrlen)
{
	algo->init_fn(c);
	git_hash_update(c, hdr, *hdrlen);
	git_hash_update(c, buf, len);
	git_hash_final_oid(oid, c);
}

static void write_object_file_prepare(const struct git_hash_algo *algo,
				      const void *buf, unsigned long len,
				      enum object_type type, struct object_id *oid,
				      char *hdr, int *hdrlen)
{
	struct git_hash_ctx c;

	/* Generate the header */
	*hdrlen = format_object_header(hdr, *hdrlen, type, len);

	/* Sha1.. */
	hash_object_body(algo, &c, buf, len, oid, hdr, hdrlen);
}

#define CHECK_COLLISION_DEST_VANISHED -2

static int check_collision(const char *source, const char *dest)
{
	char buf_source[4096], buf_dest[4096];
	int fd_source = -1, fd_dest = -1;
	int ret = 0;

	fd_source = open(source, O_RDONLY);
	if (fd_source < 0) {
		ret = error_errno(_("unable to open %s"), source);
		goto out;
	}

	fd_dest = open(dest, O_RDONLY);
	if (fd_dest < 0) {
		if (errno != ENOENT)
			ret = error_errno(_("unable to open %s"), dest);
		else
			ret = CHECK_COLLISION_DEST_VANISHED;
		goto out;
	}

	while (1) {
		ssize_t sz_a, sz_b;

		sz_a = read_in_full(fd_source, buf_source, sizeof(buf_source));
		if (sz_a < 0) {
			ret = error_errno(_("unable to read %s"), source);
			goto out;
		}

		sz_b = read_in_full(fd_dest, buf_dest, sizeof(buf_dest));
		if (sz_b < 0) {
			ret = error_errno(_("unable to read %s"), dest);
			goto out;
		}

		if (sz_a != sz_b || memcmp(buf_source, buf_dest, sz_a)) {
			ret = error(_("files '%s' and '%s' differ in contents"),
				    source, dest);
			goto out;
		}

		if ((size_t) sz_a < sizeof(buf_source))
			break;
	}

out:
	if (fd_source > -1)
		close(fd_source);
	if (fd_dest > -1)
		close(fd_dest);
	return ret;
}

/*
 * Move the just written object into its final resting place.
 */
int finalize_object_file(struct repository *repo,
			 const char *tmpfile, const char *filename)
{
	return finalize_object_file_flags(repo, tmpfile, filename, 0);
}

int finalize_object_file_flags(struct repository *repo,
			       const char *tmpfile, const char *filename,
			       enum finalize_object_file_flags flags)
{
	unsigned retries = 0;
	int ret;

retry:
	ret = 0;

	if (object_creation_mode == OBJECT_CREATION_USES_RENAMES)
		goto try_rename;
	else if (link(tmpfile, filename))
		ret = errno;
	else
		unlink_or_warn(tmpfile);

	/*
	 * Coda hack - coda doesn't like cross-directory links,
	 * so we fall back to a rename, which will mean that it
	 * won't be able to check collisions, but that's not a
	 * big deal.
	 *
	 * The same holds for FAT formatted media.
	 *
	 * When this succeeds, we just return.  We have nothing
	 * left to unlink.
	 */
	if (ret && ret != EEXIST) {
		struct stat st;

	try_rename:
		if (!stat(filename, &st))
			ret = EEXIST;
		else if (!rename(tmpfile, filename))
			goto out;
		else
			ret = errno;
	}
	if (ret) {
		if (ret != EEXIST) {
			int saved_errno = errno;
			unlink_or_warn(tmpfile);
			errno = saved_errno;
			return error_errno(_("unable to write file %s"), filename);
		}
		if (!(flags & FOF_SKIP_COLLISION_CHECK)) {
			ret = check_collision(tmpfile, filename);
			if (ret == CHECK_COLLISION_DEST_VANISHED) {
				if (retries++ > 5)
					return error(_("unable to write repeatedly vanishing file %s"),
						     filename);
				goto retry;
			}
			else if (ret)
				return -1;
		}
		unlink_or_warn(tmpfile);
	}

out:
	if (adjust_shared_perm(repo, filename))
		return error(_("unable to set permission to '%s'"), filename);
	return 0;
}

void hash_object_file(const struct git_hash_algo *algo, const void *buf,
		      unsigned long len, enum object_type type,
		      struct object_id *oid)
{
	char hdr[MAX_HEADER_LEN];
	int hdrlen = sizeof(hdr);

	write_object_file_prepare(algo, buf, len, type, oid, hdr, &hdrlen);
}

struct transaction_packfile {
	char *pack_tmp_name;
	struct hashfile *f;
	off_t offset;
	struct pack_idx_option pack_idx_opts;

	struct pack_idx_entry **written;
	uint32_t alloc_written;
	uint32_t nr_written;
};

struct odb_transaction_files {
	struct odb_transaction base;

	struct tmp_objdir *objdir;
	struct transaction_packfile packfile;
};

static void prepare_loose_object_transaction(struct odb_transaction *base)
{
	struct odb_transaction_files *transaction = (struct odb_transaction_files *)base;

	/*
	 * We lazily create the temporary object directory
	 * the first time an object might be added, since
	 * callers may not know whether any objects will be
	 * added at the time they call odb_transaction_files_begin.
	 */
	if (!transaction || transaction->objdir)
		return;

	transaction->objdir = tmp_objdir_create(base->source->odb->repo, "bulk-fsync");
	if (transaction->objdir)
		tmp_objdir_replace_primary_odb(transaction->objdir, 0);
}

static void fsync_loose_object_transaction(struct odb_transaction *base,
					   int fd, const char *filename)
{
	struct odb_transaction_files *transaction = (struct odb_transaction_files *)base;

	/*
	 * If we have an active ODB transaction, we issue a call that
	 * cleans the filesystem page cache but avoids a hardware flush
	 * command. Later on we will issue a single hardware flush
	 * before renaming the objects to their final names as part of
	 * flush_batch_fsync.
	 */
	if (!transaction || !transaction->objdir ||
	    git_fsync(fd, FSYNC_WRITEOUT_ONLY) < 0) {
		if (errno == ENOSYS)
			warning(_("core.fsyncMethod = batch is unsupported on this platform"));
		fsync_or_die(fd, filename);
	}
}

/*
 * Cleanup after batch-mode fsync_object_files.
 */
static void flush_loose_object_transaction(struct odb_transaction_files *transaction)
{
	struct strbuf temp_path = STRBUF_INIT;
	struct tempfile *temp;

	if (!transaction->objdir)
		return;

	/*
	 * Issue a full hardware flush against a temporary file to ensure
	 * that all objects are durable before any renames occur. The code in
	 * fsync_loose_object_transaction has already issued a writeout
	 * request, but it has not flushed any writeback cache in the storage
	 * hardware or any filesystem logs. This fsync call acts as a barrier
	 * to ensure that the data in each new object file is durable before
	 * the final name is visible.
	 */
	strbuf_addf(&temp_path, "%s/bulk_fsync_XXXXXX",
		    repo_get_object_directory(transaction->base.source->odb->repo));
	temp = xmks_tempfile(temp_path.buf);
	fsync_or_die(get_tempfile_fd(temp), get_tempfile_path(temp));
	delete_tempfile(&temp);
	strbuf_release(&temp_path);

	/*
	 * Make the object files visible in the primary ODB after their data is
	 * fully durable.
	 */
	tmp_objdir_migrate(transaction->objdir);
	transaction->objdir = NULL;
}

/* Finalize a file on disk, and close it. */
static void close_loose_object(struct odb_source *source,
			       int fd, const char *filename)
{
	if (source->will_destroy)
		goto out;

	if (batch_fsync_enabled(FSYNC_COMPONENT_LOOSE_OBJECT))
		fsync_loose_object_transaction(source->odb->transaction, fd, filename);
	else if (fsync_object_files > 0)
		fsync_or_die(fd, filename);
	else
		fsync_component_or_die(FSYNC_COMPONENT_LOOSE_OBJECT, fd,
				       filename);

out:
	if (close(fd) != 0)
		die_errno(_("error when closing loose object file"));
}

/* Size of directory component, including the ending '/' */
static inline int directory_size(const char *filename)
{
	const char *s = strrchr(filename, '/');
	if (!s)
		return 0;
	return s - filename + 1;
}

/*
 * This creates a temporary file in the same directory as the final
 * 'filename'
 *
 * We want to avoid cross-directory filename renames, because those
 * can have problems on various filesystems (FAT, NFS, Coda).
 */
static int create_tmpfile(struct repository *repo,
			  struct strbuf *tmp, const char *filename)
{
	int fd, dirlen = directory_size(filename);

	strbuf_reset(tmp);
	strbuf_add(tmp, filename, dirlen);
	strbuf_addstr(tmp, "tmp_obj_XXXXXX");
	fd = git_mkstemp_mode(tmp->buf, 0444);
	if (fd < 0 && dirlen && errno == ENOENT) {
		/*
		 * Make sure the directory exists; note that the contents
		 * of the buffer are undefined after mkstemp returns an
		 * error, so we have to rewrite the whole buffer from
		 * scratch.
		 */
		strbuf_reset(tmp);
		strbuf_add(tmp, filename, dirlen - 1);
		if (mkdir(tmp->buf, 0777) && errno != EEXIST)
			return -1;
		if (adjust_shared_perm(repo, tmp->buf))
			return -1;

		/* Try again */
		strbuf_addstr(tmp, "/tmp_obj_XXXXXX");
		fd = git_mkstemp_mode(tmp->buf, 0444);
	}
	return fd;
}

/**
 * Common steps for loose object writers to start writing loose
 * objects:
 *
 * - Create tmpfile for the loose object.
 * - Setup zlib stream for compression.
 * - Start to feed header to zlib stream.
 *
 * Returns a "fd", which should later be provided to
 * end_loose_object_common().
 */
static int start_loose_object_common(struct odb_source *source,
				     struct strbuf *tmp_file,
				     const char *filename, unsigned flags,
				     git_zstream *stream,
				     unsigned char *buf, size_t buflen,
				     struct git_hash_ctx *c, struct git_hash_ctx *compat_c,
				     char *hdr, int hdrlen)
{
	const struct git_hash_algo *algo = source->odb->repo->hash_algo;
	const struct git_hash_algo *compat = source->odb->repo->compat_hash_algo;
	int fd;

	fd = create_tmpfile(source->odb->repo, tmp_file, filename);
	if (fd < 0) {
		if (flags & WRITE_OBJECT_SILENT)
			return -1;
		else if (errno == EACCES)
			return error(_("insufficient permission for adding "
				       "an object to repository database %s"),
				     source->path);
		else
			return error_errno(
				_("unable to create temporary file"));
	}

	/*  Setup zlib stream for compression */
	git_deflate_init(stream, zlib_compression_level);
	stream->next_out = buf;
	stream->avail_out = buflen;
	algo->init_fn(c);
	if (compat && compat_c)
		compat->init_fn(compat_c);

	/*  Start to feed header to zlib stream */
	stream->next_in = (unsigned char *)hdr;
	stream->avail_in = hdrlen;
	while (git_deflate(stream, 0) == Z_OK)
		; /* nothing */
	git_hash_update(c, hdr, hdrlen);
	if (compat && compat_c)
		git_hash_update(compat_c, hdr, hdrlen);

	return fd;
}

/**
 * Common steps for the inner git_deflate() loop for writing loose
 * objects. Returns what git_deflate() returns.
 */
static int write_loose_object_common(struct odb_source *source,
				     struct git_hash_ctx *c, struct git_hash_ctx *compat_c,
				     git_zstream *stream, const int flush,
				     unsigned char *in0, const int fd,
				     unsigned char *compressed,
				     const size_t compressed_len)
{
	const struct git_hash_algo *compat = source->odb->repo->compat_hash_algo;
	int ret;

	ret = git_deflate(stream, flush ? Z_FINISH : 0);
	git_hash_update(c, in0, stream->next_in - in0);
	if (compat && compat_c)
		git_hash_update(compat_c, in0, stream->next_in - in0);
	if (write_in_full(fd, compressed, stream->next_out - compressed) < 0)
		die_errno(_("unable to write loose object file"));
	stream->next_out = compressed;
	stream->avail_out = compressed_len;

	return ret;
}

/**
 * Common steps for loose object writers to end writing loose objects:
 *
 * - End the compression of zlib stream.
 * - Get the calculated oid to "oid".
 */
static int end_loose_object_common(struct odb_source *source,
				   struct git_hash_ctx *c, struct git_hash_ctx *compat_c,
				   git_zstream *stream, struct object_id *oid,
				   struct object_id *compat_oid)
{
	const struct git_hash_algo *compat = source->odb->repo->compat_hash_algo;
	int ret;

	ret = git_deflate_end_gently(stream);
	if (ret != Z_OK)
		return ret;
	git_hash_final_oid(oid, c);
	if (compat && compat_c)
		git_hash_final_oid(compat_oid, compat_c);

	return Z_OK;
}

static int write_loose_object(struct odb_source *source,
			      const struct object_id *oid, char *hdr,
			      int hdrlen, const void *buf, unsigned long len,
			      time_t mtime, unsigned flags)
{
	int fd, ret;
	unsigned char compressed[4096];
	git_zstream stream;
	struct git_hash_ctx c;
	struct object_id parano_oid;
	static struct strbuf tmp_file = STRBUF_INIT;
	static struct strbuf filename = STRBUF_INIT;

	if (batch_fsync_enabled(FSYNC_COMPONENT_LOOSE_OBJECT))
		prepare_loose_object_transaction(source->odb->transaction);

	odb_loose_path(source, &filename, oid);

	fd = start_loose_object_common(source, &tmp_file, filename.buf, flags,
				       &stream, compressed, sizeof(compressed),
				       &c, NULL, hdr, hdrlen);
	if (fd < 0)
		return -1;

	/* Then the data itself.. */
	stream.next_in = (void *)buf;
	stream.avail_in = len;
	do {
		unsigned char *in0 = stream.next_in;

		ret = write_loose_object_common(source, &c, NULL, &stream, 1, in0, fd,
						compressed, sizeof(compressed));
	} while (ret == Z_OK);

	if (ret != Z_STREAM_END)
		die(_("unable to deflate new object %s (%d)"), oid_to_hex(oid),
		    ret);
	ret = end_loose_object_common(source, &c, NULL, &stream, &parano_oid, NULL);
	if (ret != Z_OK)
		die(_("deflateEnd on object %s failed (%d)"), oid_to_hex(oid),
		    ret);
	if (!oideq(oid, &parano_oid))
		die(_("confused by unstable object source data for %s"),
		    oid_to_hex(oid));

	close_loose_object(source, fd, tmp_file.buf);

	if (mtime) {
		struct utimbuf utb;
		utb.actime = mtime;
		utb.modtime = mtime;
		if (utime(tmp_file.buf, &utb) < 0 &&
		    !(flags & WRITE_OBJECT_SILENT))
			warning_errno(_("failed utime() on %s"), tmp_file.buf);
	}

	return finalize_object_file_flags(source->odb->repo, tmp_file.buf, filename.buf,
					  FOF_SKIP_COLLISION_CHECK);
}

int odb_source_loose_freshen_object(struct odb_source *source,
				    const struct object_id *oid)
{
	return !!check_and_freshen_source(source, oid, 1);
}

int odb_source_loose_write_stream(struct odb_source *source,
				  struct odb_write_stream *in_stream, size_t len,
				  struct object_id *oid)
{
	const struct git_hash_algo *compat = source->odb->repo->compat_hash_algo;
	struct object_id compat_oid;
	int fd, ret, err = 0, flush = 0;
	unsigned char compressed[4096];
	git_zstream stream;
	struct git_hash_ctx c, compat_c;
	struct strbuf tmp_file = STRBUF_INIT;
	struct strbuf filename = STRBUF_INIT;
	int dirlen;
	char hdr[MAX_HEADER_LEN];
	int hdrlen;

	if (batch_fsync_enabled(FSYNC_COMPONENT_LOOSE_OBJECT))
		prepare_loose_object_transaction(source->odb->transaction);

	/* Since oid is not determined, save tmp file to odb path. */
	strbuf_addf(&filename, "%s/", source->path);
	hdrlen = format_object_header(hdr, sizeof(hdr), OBJ_BLOB, len);

	/*
	 * Common steps for write_loose_object and stream_loose_object to
	 * start writing loose objects:
	 *
	 *  - Create tmpfile for the loose object.
	 *  - Setup zlib stream for compression.
	 *  - Start to feed header to zlib stream.
	 */
	fd = start_loose_object_common(source, &tmp_file, filename.buf, 0,
				       &stream, compressed, sizeof(compressed),
				       &c, &compat_c, hdr, hdrlen);
	if (fd < 0) {
		err = -1;
		goto cleanup;
	}

	/* Then the data itself.. */
	do {
		unsigned char *in0 = stream.next_in;

		if (!stream.avail_in && !in_stream->is_finished) {
			const void *in = in_stream->read(in_stream, &stream.avail_in);
			stream.next_in = (void *)in;
			in0 = (unsigned char *)in;
			/* All data has been read. */
			if (in_stream->is_finished)
				flush = 1;
		}
		ret = write_loose_object_common(source, &c, &compat_c, &stream, flush, in0, fd,
						compressed, sizeof(compressed));
		/*
		 * Unlike write_loose_object(), we do not have the entire
		 * buffer. If we get Z_BUF_ERROR due to too few input bytes,
		 * then we'll replenish them in the next input_stream->read()
		 * call when we loop.
		 */
	} while (ret == Z_OK || ret == Z_BUF_ERROR);

	if (stream.total_in != len + hdrlen)
		die(_("write stream object %ld != %"PRIuMAX), stream.total_in,
		    (uintmax_t)len + hdrlen);

	/*
	 * Common steps for write_loose_object and stream_loose_object to
	 * end writing loose object:
	 *
	 *  - End the compression of zlib stream.
	 *  - Get the calculated oid.
	 */
	if (ret != Z_STREAM_END)
		die(_("unable to stream deflate new object (%d)"), ret);
	ret = end_loose_object_common(source, &c, &compat_c, &stream, oid, &compat_oid);
	if (ret != Z_OK)
		die(_("deflateEnd on stream object failed (%d)"), ret);
	close_loose_object(source, fd, tmp_file.buf);

	if (odb_freshen_object(source->odb, oid)) {
		unlink_or_warn(tmp_file.buf);
		goto cleanup;
	}
	odb_loose_path(source, &filename, oid);

	/* We finally know the object path, and create the missing dir. */
	dirlen = directory_size(filename.buf);
	if (dirlen) {
		struct strbuf dir = STRBUF_INIT;
		strbuf_add(&dir, filename.buf, dirlen);

		if (safe_create_dir_in_gitdir(source->odb->repo, dir.buf) &&
		    errno != EEXIST) {
			err = error_errno(_("unable to create directory %s"), dir.buf);
			strbuf_release(&dir);
			goto cleanup;
		}
		strbuf_release(&dir);
	}

	err = finalize_object_file_flags(source->odb->repo, tmp_file.buf, filename.buf,
					 FOF_SKIP_COLLISION_CHECK);
	if (!err && compat)
		err = repo_add_loose_object_map(source, oid, &compat_oid);
cleanup:
	strbuf_release(&tmp_file);
	strbuf_release(&filename);
	return err;
}

int odb_source_loose_write_object(struct odb_source *source,
				  const void *buf, unsigned long len,
				  enum object_type type, struct object_id *oid,
				  struct object_id *compat_oid_in, unsigned flags)
{
	const struct git_hash_algo *algo = source->odb->repo->hash_algo;
	const struct git_hash_algo *compat = source->odb->repo->compat_hash_algo;
	struct object_id compat_oid;
	char hdr[MAX_HEADER_LEN];
	int hdrlen = sizeof(hdr);

	/* Generate compat_oid */
	if (compat) {
		if (compat_oid_in)
			oidcpy(&compat_oid, compat_oid_in);
		else if (type == OBJ_BLOB)
			hash_object_file(compat, buf, len, type, &compat_oid);
		else {
			struct strbuf converted = STRBUF_INIT;
			convert_object_file(source->odb->repo, &converted, algo, compat,
					    buf, len, type, 0);
			hash_object_file(compat, converted.buf, converted.len,
					 type, &compat_oid);
			strbuf_release(&converted);
		}
	}

	/* Normally if we have it in the pack then we do not bother writing
	 * it out into .git/objects/??/?{38} file.
	 */
	write_object_file_prepare(algo, buf, len, type, oid, hdr, &hdrlen);
	if (odb_freshen_object(source->odb, oid))
		return 0;
	if (write_loose_object(source, oid, hdr, hdrlen, buf, len, 0, flags))
		return -1;
	if (compat)
		return repo_add_loose_object_map(source, oid, &compat_oid);
	return 0;
}

int force_object_loose(struct odb_source *source,
		       const struct object_id *oid, time_t mtime)
{
	const struct git_hash_algo *compat = source->odb->repo->compat_hash_algo;
	void *buf;
	unsigned long len;
	struct object_info oi = OBJECT_INFO_INIT;
	struct object_id compat_oid;
	enum object_type type;
	char hdr[MAX_HEADER_LEN];
	int hdrlen;
	int ret;

	for (struct odb_source *s = source->odb->sources; s; s = s->next)
		if (odb_source_loose_has_object(s, oid))
			return 0;

	oi.typep = &type;
	oi.sizep = &len;
	oi.contentp = &buf;
	if (odb_read_object_info_extended(source->odb, oid, &oi, 0))
		return error(_("cannot read object for %s"), oid_to_hex(oid));
	if (compat) {
		if (repo_oid_to_algop(source->odb->repo, oid, compat, &compat_oid))
			return error(_("cannot map object %s to %s"),
				     oid_to_hex(oid), compat->name);
	}
	hdrlen = format_object_header(hdr, sizeof(hdr), type, len);
	ret = write_loose_object(source, oid, hdr, hdrlen, buf, len, mtime, 0);
	if (!ret && compat)
		ret = repo_add_loose_object_map(source, oid, &compat_oid);
	free(buf);

	return ret;
}

/*
 * We can't use the normal fsck_error_function() for index_mem(),
 * because we don't yet have a valid oid for it to report. Instead,
 * report the minimal fsck error here, and rely on the caller to
 * give more context.
 */
static int hash_format_check_report(struct fsck_options *opts UNUSED,
				    void *fsck_report UNUSED,
				    enum fsck_msg_type msg_type UNUSED,
				    enum fsck_msg_id msg_id UNUSED,
				    const char *message)
{
	error(_("object fails fsck: %s"), message);
	return 1;
}

static int index_mem(struct index_state *istate,
		     struct object_id *oid,
		     const void *buf, size_t size,
		     enum object_type type,
		     const char *path, unsigned flags)
{
	struct strbuf nbuf = STRBUF_INIT;
	int ret = 0;
	int write_object = flags & INDEX_WRITE_OBJECT;

	if (!type)
		type = OBJ_BLOB;

	/*
	 * Convert blobs to git internal format
	 */
	if ((type == OBJ_BLOB) && path) {
		if (convert_to_git(istate, path, buf, size, &nbuf,
				   get_conv_flags(flags))) {
			buf = nbuf.buf;
			size = nbuf.len;
		}
	}
	if (flags & INDEX_FORMAT_CHECK) {
		struct fsck_options opts = FSCK_OPTIONS_DEFAULT;

		opts.strict = 1;
		opts.error_func = hash_format_check_report;
		if (fsck_buffer(null_oid(istate->repo->hash_algo), type, buf, size, &opts))
			die(_("refusing to create malformed object"));
		fsck_finish(&opts);
	}

	if (write_object)
		ret = odb_write_object(istate->repo->objects, buf, size, type, oid);
	else
		hash_object_file(istate->repo->hash_algo, buf, size, type, oid);

	strbuf_release(&nbuf);
	return ret;
}

static int index_stream_convert_blob(struct index_state *istate,
				     struct object_id *oid,
				     int fd,
				     const char *path,
				     unsigned flags)
{
	int ret = 0;
	const int write_object = flags & INDEX_WRITE_OBJECT;
	struct strbuf sbuf = STRBUF_INIT;

	assert(path);
	ASSERT(would_convert_to_git_filter_fd(istate, path));

	convert_to_git_filter_fd(istate, path, fd, &sbuf,
				 get_conv_flags(flags));

	if (write_object)
		ret = odb_write_object(istate->repo->objects, sbuf.buf, sbuf.len, OBJ_BLOB,
				       oid);
	else
		hash_object_file(istate->repo->hash_algo, sbuf.buf, sbuf.len, OBJ_BLOB,
				 oid);
	strbuf_release(&sbuf);
	return ret;
}

static int index_pipe(struct index_state *istate, struct object_id *oid,
		      int fd, enum object_type type,
		      const char *path, unsigned flags)
{
	struct strbuf sbuf = STRBUF_INIT;
	int ret;

	if (strbuf_read(&sbuf, fd, 4096) >= 0)
		ret = index_mem(istate, oid, sbuf.buf, sbuf.len, type, path, flags);
	else
		ret = -1;
	strbuf_release(&sbuf);
	return ret;
}

#define SMALL_FILE_SIZE (32*1024)

static int index_core(struct index_state *istate,
		      struct object_id *oid, int fd, size_t size,
		      enum object_type type, const char *path,
		      unsigned flags)
{
	int ret;

	if (!size) {
		ret = index_mem(istate, oid, "", size, type, path, flags);
	} else if (size <= SMALL_FILE_SIZE) {
		char *buf = xmalloc(size);
		ssize_t read_result = read_in_full(fd, buf, size);
		if (read_result < 0)
			ret = error_errno(_("read error while indexing %s"),
					  path ? path : "<unknown>");
		else if ((size_t) read_result != size)
			ret = error(_("short read while indexing %s"),
				    path ? path : "<unknown>");
		else
			ret = index_mem(istate, oid, buf, size, type, path, flags);
		free(buf);
	} else {
		void *buf = xmmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
		ret = index_mem(istate, oid, buf, size, type, path, flags);
		munmap(buf, size);
	}
	return ret;
}

static int already_written(struct odb_transaction_files *transaction,
			   struct object_id *oid)
{
	/* The object may already exist in the repository */
	if (odb_has_object(transaction->base.source->odb, oid,
			   HAS_OBJECT_RECHECK_PACKED | HAS_OBJECT_FETCH_PROMISOR))
		return 1;

	/* Might want to keep the list sorted */
	for (uint32_t i = 0; i < transaction->packfile.nr_written; i++)
		if (oideq(&transaction->packfile.written[i]->oid, oid))
			return 1;

	/* This is a new object we need to keep */
	return 0;
}

/* Lazily create backing packfile for the state */
static void prepare_packfile_transaction(struct odb_transaction_files *transaction,
					 unsigned flags)
{
	struct transaction_packfile *state = &transaction->packfile;
	if (!(flags & INDEX_WRITE_OBJECT) || state->f)
		return;

	state->f = create_tmp_packfile(transaction->base.source->odb->repo,
				       &state->pack_tmp_name);
	reset_pack_idx_option(&state->pack_idx_opts);

	/* Pretend we are going to write only one object */
	state->offset = write_pack_header(state->f, 1);
	if (!state->offset)
		die_errno("unable to write pack header");
}

/*
 * Read the contents from fd for size bytes, streaming it to the
 * packfile in state while updating the hash in ctx. Signal a failure
 * by returning a negative value when the resulting pack would exceed
 * the pack size limit and this is not the first object in the pack,
 * so that the caller can discard what we wrote from the current pack
 * by truncating it and opening a new one. The caller will then call
 * us again after rewinding the input fd.
 *
 * The already_hashed_to pointer is kept untouched by the caller to
 * make sure we do not hash the same byte when we are called
 * again. This way, the caller does not have to checkpoint its hash
 * status before calling us just in case we ask it to call us again
 * with a new pack.
 */
static int stream_blob_to_pack(struct transaction_packfile *state,
			       struct git_hash_ctx *ctx, off_t *already_hashed_to,
			       int fd, size_t size, const char *path,
			       unsigned flags)
{
	git_zstream s;
	unsigned char ibuf[16384];
	unsigned char obuf[16384];
	unsigned hdrlen;
	int status = Z_OK;
	int write_object = (flags & INDEX_WRITE_OBJECT);
	off_t offset = 0;

	git_deflate_init(&s, pack_compression_level);

	hdrlen = encode_in_pack_object_header(obuf, sizeof(obuf), OBJ_BLOB, size);
	s.next_out = obuf + hdrlen;
	s.avail_out = sizeof(obuf) - hdrlen;

	while (status != Z_STREAM_END) {
		if (size && !s.avail_in) {
			size_t rsize = size < sizeof(ibuf) ? size : sizeof(ibuf);
			ssize_t read_result = read_in_full(fd, ibuf, rsize);
			if (read_result < 0)
				die_errno("failed to read from '%s'", path);
			if ((size_t)read_result != rsize)
				die("failed to read %u bytes from '%s'",
				    (unsigned)rsize, path);
			offset += rsize;
			if (*already_hashed_to < offset) {
				size_t hsize = offset - *already_hashed_to;
				if (rsize < hsize)
					hsize = rsize;
				if (hsize)
					git_hash_update(ctx, ibuf, hsize);
				*already_hashed_to = offset;
			}
			s.next_in = ibuf;
			s.avail_in = rsize;
			size -= rsize;
		}

		status = git_deflate(&s, size ? 0 : Z_FINISH);

		if (!s.avail_out || status == Z_STREAM_END) {
			if (write_object) {
				size_t written = s.next_out - obuf;

				/* would we bust the size limit? */
				if (state->nr_written &&
				    pack_size_limit_cfg &&
				    pack_size_limit_cfg < state->offset + written) {
					git_deflate_abort(&s);
					return -1;
				}

				hashwrite(state->f, obuf, written);
				state->offset += written;
			}
			s.next_out = obuf;
			s.avail_out = sizeof(obuf);
		}

		switch (status) {
		case Z_OK:
		case Z_BUF_ERROR:
		case Z_STREAM_END:
			continue;
		default:
			die("unexpected deflate failure: %d", status);
		}
	}
	git_deflate_end(&s);
	return 0;
}

static void flush_packfile_transaction(struct odb_transaction_files *transaction)
{
	struct transaction_packfile *state = &transaction->packfile;
	struct repository *repo = transaction->base.source->odb->repo;
	unsigned char hash[GIT_MAX_RAWSZ];
	struct strbuf packname = STRBUF_INIT;
	char *idx_tmp_name = NULL;

	if (!state->f)
		return;

	if (state->nr_written == 0) {
		close(state->f->fd);
		free_hashfile(state->f);
		unlink(state->pack_tmp_name);
		goto clear_exit;
	} else if (state->nr_written == 1) {
		finalize_hashfile(state->f, hash, FSYNC_COMPONENT_PACK,
				  CSUM_HASH_IN_STREAM | CSUM_FSYNC | CSUM_CLOSE);
	} else {
		int fd = finalize_hashfile(state->f, hash, FSYNC_COMPONENT_PACK, 0);
		fixup_pack_header_footer(repo->hash_algo, fd, hash, state->pack_tmp_name,
					 state->nr_written, hash,
					 state->offset);
		close(fd);
	}

	strbuf_addf(&packname, "%s/pack/pack-%s.",
		    repo_get_object_directory(transaction->base.source->odb->repo),
		    hash_to_hex_algop(hash, repo->hash_algo));

	stage_tmp_packfiles(repo, &packname, state->pack_tmp_name,
			    state->written, state->nr_written, NULL,
			    &state->pack_idx_opts, hash, &idx_tmp_name);
	rename_tmp_packfile_idx(repo, &packname, &idx_tmp_name);

	for (uint32_t i = 0; i < state->nr_written; i++)
		free(state->written[i]);

clear_exit:
	free(idx_tmp_name);
	free(state->pack_tmp_name);
	free(state->written);
	memset(state, 0, sizeof(*state));

	strbuf_release(&packname);
	/* Make objects we just wrote available to ourselves */
	odb_reprepare(repo->objects);
}

/*
 * This writes the specified object to a packfile. Objects written here
 * during the same transaction are written to the same packfile. The
 * packfile is not flushed until the transaction is flushed. The caller
 * is expected to ensure a valid transaction is setup for objects to be
 * recorded to.
 *
 * This also bypasses the usual "convert-to-git" dance, and that is on
 * purpose. We could write a streaming version of the converting
 * functions and insert that before feeding the data to fast-import
 * (or equivalent in-core API described above). However, that is
 * somewhat complicated, as we do not know the size of the filter
 * result, which we need to know beforehand when writing a git object.
 * Since the primary motivation for trying to stream from the working
 * tree file and to avoid mmaping it in core is to deal with large
 * binary blobs, they generally do not want to get any conversion, and
 * callers should avoid this code path when filters are requested.
 */
static int index_blob_packfile_transaction(struct odb_transaction_files *transaction,
					   struct object_id *result_oid, int fd,
					   size_t size, const char *path,
					   unsigned flags)
{
	struct transaction_packfile *state = &transaction->packfile;
	off_t seekback, already_hashed_to;
	struct git_hash_ctx ctx;
	unsigned char obuf[16384];
	unsigned header_len;
	struct hashfile_checkpoint checkpoint;
	struct pack_idx_entry *idx = NULL;

	seekback = lseek(fd, 0, SEEK_CUR);
	if (seekback == (off_t)-1)
		return error("cannot find the current offset");

	header_len = format_object_header((char *)obuf, sizeof(obuf),
					  OBJ_BLOB, size);
	transaction->base.source->odb->repo->hash_algo->init_fn(&ctx);
	git_hash_update(&ctx, obuf, header_len);

	/* Note: idx is non-NULL when we are writing */
	if ((flags & INDEX_WRITE_OBJECT) != 0) {
		CALLOC_ARRAY(idx, 1);

		prepare_packfile_transaction(transaction, flags);
		hashfile_checkpoint_init(state->f, &checkpoint);
	}

	already_hashed_to = 0;

	while (1) {
		prepare_packfile_transaction(transaction, flags);
		if (idx) {
			hashfile_checkpoint(state->f, &checkpoint);
			idx->offset = state->offset;
			crc32_begin(state->f);
		}
		if (!stream_blob_to_pack(state, &ctx, &already_hashed_to,
					 fd, size, path, flags))
			break;
		/*
		 * Writing this object to the current pack will make
		 * it too big; we need to truncate it, start a new
		 * pack, and write into it.
		 */
		if (!idx)
			BUG("should not happen");
		hashfile_truncate(state->f, &checkpoint);
		state->offset = checkpoint.offset;
		flush_packfile_transaction(transaction);
		if (lseek(fd, seekback, SEEK_SET) == (off_t)-1)
			return error("cannot seek back");
	}
	git_hash_final_oid(result_oid, &ctx);
	if (!idx)
		return 0;

	idx->crc32 = crc32_end(state->f);
	if (already_written(transaction, result_oid)) {
		hashfile_truncate(state->f, &checkpoint);
		state->offset = checkpoint.offset;
		free(idx);
	} else {
		oidcpy(&idx->oid, result_oid);
		ALLOC_GROW(state->written,
			   state->nr_written + 1,
			   state->alloc_written);
		state->written[state->nr_written++] = idx;
	}
	return 0;
}

int index_fd(struct index_state *istate, struct object_id *oid,
	     int fd, struct stat *st,
	     enum object_type type, const char *path, unsigned flags)
{
	int ret;

	/*
	 * Call xsize_t() only when needed to avoid potentially unnecessary
	 * die() for large files.
	 */
	if (type == OBJ_BLOB && path && would_convert_to_git_filter_fd(istate, path)) {
		ret = index_stream_convert_blob(istate, oid, fd, path, flags);
	} else if (!S_ISREG(st->st_mode)) {
		ret = index_pipe(istate, oid, fd, type, path, flags);
	} else if ((st->st_size >= 0 &&
		    (size_t)st->st_size <= repo_settings_get_big_file_threshold(istate->repo)) ||
		   type != OBJ_BLOB ||
		   (path && would_convert_to_git(istate, path))) {
		ret = index_core(istate, oid, fd, xsize_t(st->st_size),
				 type, path, flags);
	} else {
		struct object_database *odb = the_repository->objects;
		struct odb_transaction *transaction;

		transaction = odb_transaction_begin(odb);
		ret = index_blob_packfile_transaction((struct odb_transaction_files *)odb->transaction,
						      oid, fd,
						      xsize_t(st->st_size),
						      path, flags);
		odb_transaction_commit(transaction);
	}

	close(fd);
	return ret;
}

int index_path(struct index_state *istate, struct object_id *oid,
	       const char *path, struct stat *st, unsigned flags)
{
	int fd;
	struct strbuf sb = STRBUF_INIT;
	int rc = 0;

	switch (st->st_mode & S_IFMT) {
	case S_IFREG:
		fd = open(path, O_RDONLY);
		if (fd < 0)
			return error_errno("open(\"%s\")", path);
		if (index_fd(istate, oid, fd, st, OBJ_BLOB, path, flags) < 0)
			return error(_("%s: failed to insert into database"),
				     path);
		break;
	case S_IFLNK:
		if (strbuf_readlink(&sb, path, st->st_size))
			return error_errno("readlink(\"%s\")", path);
		if (!(flags & INDEX_WRITE_OBJECT))
			hash_object_file(istate->repo->hash_algo, sb.buf, sb.len,
					 OBJ_BLOB, oid);
		else if (odb_write_object(istate->repo->objects, sb.buf, sb.len, OBJ_BLOB, oid))
			rc = error(_("%s: failed to insert into database"), path);
		strbuf_release(&sb);
		break;
	case S_IFDIR:
		if (repo_resolve_gitlink_ref(istate->repo, path, "HEAD", oid))
			return error(_("'%s' does not have a commit checked out"), path);
		if (&hash_algos[oid->algo] != istate->repo->hash_algo)
			return error(_("cannot add a submodule of a different hash algorithm"));
		break;
	default:
		return error(_("%s: unsupported file type"), path);
	}
	return rc;
}

int read_pack_header(int fd, struct pack_header *header)
{
	if (read_in_full(fd, header, sizeof(*header)) != sizeof(*header))
		/* "eof before pack header was fully read" */
		return PH_ERROR_EOF;

	if (header->hdr_signature != htonl(PACK_SIGNATURE))
		/* "protocol error (pack signature mismatch detected)" */
		return PH_ERROR_PACK_SIGNATURE;
	if (!pack_version_ok(header->hdr_version))
		/* "protocol error (pack version unsupported)" */
		return PH_ERROR_PROTOCOL;
	return 0;
}

static int for_each_file_in_obj_subdir(unsigned int subdir_nr,
				       struct strbuf *path,
				       const struct git_hash_algo *algop,
				       each_loose_object_fn obj_cb,
				       each_loose_cruft_fn cruft_cb,
				       each_loose_subdir_fn subdir_cb,
				       void *data)
{
	size_t origlen, baselen;
	DIR *dir;
	struct dirent *de;
	int r = 0;
	struct object_id oid;

	if (subdir_nr > 0xff)
		BUG("invalid loose object subdirectory: %x", subdir_nr);

	origlen = path->len;
	strbuf_complete(path, '/');
	strbuf_addf(path, "%02x", subdir_nr);

	dir = opendir(path->buf);
	if (!dir) {
		if (errno != ENOENT)
			r = error_errno(_("unable to open %s"), path->buf);
		strbuf_setlen(path, origlen);
		return r;
	}

	oid.hash[0] = subdir_nr;
	strbuf_addch(path, '/');
	baselen = path->len;

	while ((de = readdir_skip_dot_and_dotdot(dir))) {
		size_t namelen;

		namelen = strlen(de->d_name);
		strbuf_setlen(path, baselen);
		strbuf_add(path, de->d_name, namelen);
		if (namelen == algop->hexsz - 2 &&
		    !hex_to_bytes(oid.hash + 1, de->d_name,
				  algop->rawsz - 1)) {
			oid_set_algo(&oid, algop);
			memset(oid.hash + algop->rawsz, 0,
			       GIT_MAX_RAWSZ - algop->rawsz);
			if (obj_cb) {
				r = obj_cb(&oid, path->buf, data);
				if (r)
					break;
			}
			continue;
		}

		if (cruft_cb) {
			r = cruft_cb(de->d_name, path->buf, data);
			if (r)
				break;
		}
	}
	closedir(dir);

	strbuf_setlen(path, baselen - 1);
	if (!r && subdir_cb)
		r = subdir_cb(subdir_nr, path->buf, data);

	strbuf_setlen(path, origlen);

	return r;
}

int for_each_loose_file_in_source(struct odb_source *source,
				  each_loose_object_fn obj_cb,
				  each_loose_cruft_fn cruft_cb,
				  each_loose_subdir_fn subdir_cb,
				  void *data)
{
	struct strbuf buf = STRBUF_INIT;
	int r;

	strbuf_addstr(&buf, source->path);
	for (int i = 0; i < 256; i++) {
		r = for_each_file_in_obj_subdir(i, &buf, source->odb->repo->hash_algo,
						obj_cb, cruft_cb, subdir_cb, data);
		if (r)
			break;
	}

	strbuf_release(&buf);
	return r;
}

struct for_each_object_wrapper_data {
	struct odb_source *source;
	const struct object_info *request;
	odb_for_each_object_cb cb;
	void *cb_data;
};

static int for_each_object_wrapper_cb(const struct object_id *oid,
				      const char *path,
				      void *cb_data)
{
	struct for_each_object_wrapper_data *data = cb_data;

	if (data->request) {
		struct object_info oi = *data->request;

		if (read_object_info_from_path(data->source, path, oid, &oi, 0) < 0)
			return -1;

		return data->cb(oid, &oi, data->cb_data);
	} else {
		return data->cb(oid, NULL, data->cb_data);
	}
}

int odb_source_loose_for_each_object(struct odb_source *source,
				     const struct object_info *request,
				     odb_for_each_object_cb cb,
				     void *cb_data,
				     unsigned flags)
{
	struct for_each_object_wrapper_data data = {
		.source = source,
		.request = request,
		.cb = cb,
		.cb_data = cb_data,
	};

	/* There are no loose promisor objects, so we can return immediately. */
	if ((flags & ODB_FOR_EACH_OBJECT_PROMISOR_ONLY))
		return 0;
	if ((flags & ODB_FOR_EACH_OBJECT_LOCAL_ONLY) && !source->local)
		return 0;

	return for_each_loose_file_in_source(source, for_each_object_wrapper_cb,
					     NULL, NULL, &data);
}

static int append_loose_object(const struct object_id *oid,
			       const char *path UNUSED,
			       void *data)
{
	oidtree_insert(data, oid);
	return 0;
}

struct oidtree *odb_source_loose_cache(struct odb_source *source,
				       const struct object_id *oid)
{
	int subdir_nr = oid->hash[0];
	struct strbuf buf = STRBUF_INIT;
	size_t word_bits = bitsizeof(source->loose->subdir_seen[0]);
	size_t word_index = subdir_nr / word_bits;
	size_t mask = (size_t)1u << (subdir_nr % word_bits);
	uint32_t *bitmap;

	if (subdir_nr < 0 ||
	    (size_t) subdir_nr >= bitsizeof(source->loose->subdir_seen))
		BUG("subdir_nr out of range");

	bitmap = &source->loose->subdir_seen[word_index];
	if (*bitmap & mask)
		return source->loose->cache;
	if (!source->loose->cache) {
		ALLOC_ARRAY(source->loose->cache, 1);
		oidtree_init(source->loose->cache);
	}
	strbuf_addstr(&buf, source->path);
	for_each_file_in_obj_subdir(subdir_nr, &buf,
				    source->odb->repo->hash_algo,
				    append_loose_object,
				    NULL, NULL,
				    source->loose->cache);
	*bitmap |= mask;
	strbuf_release(&buf);
	return source->loose->cache;
}

static void odb_source_loose_clear_cache(struct odb_source_loose *loose)
{
	oidtree_clear(loose->cache);
	FREE_AND_NULL(loose->cache);
	memset(&loose->subdir_seen, 0,
	       sizeof(loose->subdir_seen));
}

void odb_source_loose_reprepare(struct odb_source *source)
{
	odb_source_loose_clear_cache(source->loose);
}

static int check_stream_oid(git_zstream *stream,
			    const char *hdr,
			    unsigned long size,
			    const char *path,
			    const struct object_id *expected_oid,
			    const struct git_hash_algo *algop)
{
	struct git_hash_ctx c;
	struct object_id real_oid;
	unsigned char buf[4096];
	unsigned long total_read;
	int status = Z_OK;

	algop->init_fn(&c);
	git_hash_update(&c, hdr, stream->total_out);

	/*
	 * We already read some bytes into hdr, but the ones up to the NUL
	 * do not count against the object's content size.
	 */
	total_read = stream->total_out - strlen(hdr) - 1;

	/*
	 * This size comparison must be "<=" to read the final zlib packets;
	 * see the comment in unpack_loose_rest for details.
	 */
	while (total_read <= size &&
	       (status == Z_OK ||
		(status == Z_BUF_ERROR && !stream->avail_out))) {
		stream->next_out = buf;
		stream->avail_out = sizeof(buf);
		if (size - total_read < stream->avail_out)
			stream->avail_out = size - total_read;
		status = git_inflate(stream, Z_FINISH);
		git_hash_update(&c, buf, stream->next_out - buf);
		total_read += stream->next_out - buf;
	}

	if (status != Z_STREAM_END) {
		error(_("corrupt loose object '%s'"), oid_to_hex(expected_oid));
		return -1;
	}
	if (stream->avail_in) {
		error(_("garbage at end of loose object '%s'"),
		      oid_to_hex(expected_oid));
		return -1;
	}

	git_hash_final_oid(&real_oid, &c);
	if (!oideq(expected_oid, &real_oid)) {
		error(_("hash mismatch for %s (expected %s)"), path,
		      oid_to_hex(expected_oid));
		return -1;
	}

	return 0;
}

int read_loose_object(struct repository *repo,
		      const char *path,
		      const struct object_id *expected_oid,
		      struct object_id *real_oid,
		      void **contents,
		      struct object_info *oi)
{
	int ret = -1;
	int fd;
	void *map = NULL;
	unsigned long mapsize;
	git_zstream stream;
	char hdr[MAX_HEADER_LEN];
	unsigned long *size = oi->sizep;

	fd = git_open(path);
	if (fd >= 0)
		map = map_fd(fd, path, &mapsize);
	if (!map) {
		error_errno(_("unable to mmap %s"), path);
		goto out;
	}

	if (unpack_loose_header(&stream, map, mapsize, hdr, sizeof(hdr)) != ULHR_OK) {
		error(_("unable to unpack header of %s"), path);
		goto out_inflate;
	}

	if (parse_loose_header(hdr, oi) < 0) {
		error(_("unable to parse header of %s"), path);
		goto out_inflate;
	}

	if (*oi->typep < 0) {
		error(_("unable to parse type from header '%s' of %s"),
		      hdr, path);
		goto out_inflate;
	}

	if (*oi->typep == OBJ_BLOB &&
	    *size > repo_settings_get_big_file_threshold(repo)) {
		if (check_stream_oid(&stream, hdr, *size, path, expected_oid,
				     repo->hash_algo) < 0)
			goto out_inflate;
	} else {
		*contents = unpack_loose_rest(&stream, hdr, *size, expected_oid);
		if (!*contents) {
			error(_("unable to unpack contents of %s"), path);
			goto out_inflate;
		}
		hash_object_file(repo->hash_algo,
				 *contents, *size,
				 *oi->typep, real_oid);
		if (!oideq(expected_oid, real_oid))
			goto out_inflate;
	}

	ret = 0; /* everything checks out */

out_inflate:
	git_inflate_end(&stream);
out:
	if (map)
		munmap(map, mapsize);
	return ret;
}

static void odb_transaction_files_commit(struct odb_transaction *base)
{
	struct odb_transaction_files *transaction = (struct odb_transaction_files *)base;

	flush_loose_object_transaction(transaction);
	flush_packfile_transaction(transaction);
}

struct odb_transaction *odb_transaction_files_begin(struct odb_source *source)
{
	struct odb_transaction_files *transaction;
	struct object_database *odb = source->odb;

	if (odb->transaction)
		return NULL;

	transaction = xcalloc(1, sizeof(*transaction));
	transaction->base.source = source;
	transaction->base.commit = odb_transaction_files_commit;

	return &transaction->base;
}

struct odb_source_loose *odb_source_loose_new(struct odb_source *source)
{
	struct odb_source_loose *loose;
	CALLOC_ARRAY(loose, 1);
	loose->source = source;
	return loose;
}

void odb_source_loose_free(struct odb_source_loose *loose)
{
	if (!loose)
		return;
	odb_source_loose_clear_cache(loose);
	loose_object_map_clear(&loose->map);
	free(loose);
}

struct odb_loose_read_stream {
	struct odb_read_stream base;
	git_zstream z;
	enum {
		ODB_LOOSE_READ_STREAM_INUSE,
		ODB_LOOSE_READ_STREAM_DONE,
		ODB_LOOSE_READ_STREAM_ERROR,
	} z_state;
	void *mapped;
	unsigned long mapsize;
	char hdr[32];
	int hdr_avail;
	int hdr_used;
};

static ssize_t read_istream_loose(struct odb_read_stream *_st, char *buf, size_t sz)
{
	struct odb_loose_read_stream *st = (struct odb_loose_read_stream *)_st;
	size_t total_read = 0;

	switch (st->z_state) {
	case ODB_LOOSE_READ_STREAM_DONE:
		return 0;
	case ODB_LOOSE_READ_STREAM_ERROR:
		return -1;
	default:
		break;
	}

	if (st->hdr_used < st->hdr_avail) {
		size_t to_copy = st->hdr_avail - st->hdr_used;
		if (sz < to_copy)
			to_copy = sz;
		memcpy(buf, st->hdr + st->hdr_used, to_copy);
		st->hdr_used += to_copy;
		total_read += to_copy;
	}

	while (total_read < sz) {
		int status;

		st->z.next_out = (unsigned char *)buf + total_read;
		st->z.avail_out = sz - total_read;
		status = git_inflate(&st->z, Z_FINISH);

		total_read = st->z.next_out - (unsigned char *)buf;

		if (status == Z_STREAM_END) {
			git_inflate_end(&st->z);
			st->z_state = ODB_LOOSE_READ_STREAM_DONE;
			break;
		}
		if (status != Z_OK && (status != Z_BUF_ERROR || total_read < sz)) {
			git_inflate_end(&st->z);
			st->z_state = ODB_LOOSE_READ_STREAM_ERROR;
			return -1;
		}
	}
	return total_read;
}

static int close_istream_loose(struct odb_read_stream *_st)
{
	struct odb_loose_read_stream *st = (struct odb_loose_read_stream *)_st;
	if (st->z_state == ODB_LOOSE_READ_STREAM_INUSE)
		git_inflate_end(&st->z);
	munmap(st->mapped, st->mapsize);
	return 0;
}

int odb_source_loose_read_object_stream(struct odb_read_stream **out,
					struct odb_source *source,
					const struct object_id *oid)
{
	struct object_info oi = OBJECT_INFO_INIT;
	struct odb_loose_read_stream *st;
	unsigned long mapsize;
	void *mapped;

	mapped = odb_source_loose_map_object(source, oid, &mapsize);
	if (!mapped)
		return -1;

	/*
	 * Note: we must allocate this structure early even though we may still
	 * fail. This is because we need to initialize the zlib stream, and it
	 * is not possible to copy the stream around after the fact because it
	 * has self-referencing pointers.
	 */
	CALLOC_ARRAY(st, 1);

	switch (unpack_loose_header(&st->z, mapped, mapsize, st->hdr,
				    sizeof(st->hdr))) {
	case ULHR_OK:
		break;
	case ULHR_BAD:
	case ULHR_TOO_LONG:
		goto error;
	}

	oi.sizep = &st->base.size;
	oi.typep = &st->base.type;

	if (parse_loose_header(st->hdr, &oi) < 0 || st->base.type < 0)
		goto error;

	st->mapped = mapped;
	st->mapsize = mapsize;
	st->hdr_used = strlen(st->hdr) + 1;
	st->hdr_avail = st->z.total_out;
	st->z_state = ODB_LOOSE_READ_STREAM_INUSE;
	st->base.close = close_istream_loose;
	st->base.read = read_istream_loose;

	*out = &st->base;

	return 0;
error:
	git_inflate_end(&st->z);
	munmap(st->mapped, st->mapsize);
	free(st);
	return -1;
}
