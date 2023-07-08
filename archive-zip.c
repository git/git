/*
 * Copyright (c) 2006 Rene Scharfe
 */
#include "git-compat-util.h"
#include "config.h"
#include "archive.h"
#include "gettext.h"
#include "git-zlib.h"
#include "hex.h"
#include "streaming.h"
#include "utf8.h"
#include "object-store-ll.h"
#include "userdiff.h"
#include "write-or-die.h"
#include "xdiff-interface.h"
#include "date.h"

static int zip_date;
static int zip_time;

/* We only care about the "buf" part here. */
static struct strbuf zip_dir;

static uintmax_t zip_offset;
static uint64_t zip_dir_entries;

static unsigned int max_creator_version;

#define ZIP_STREAM	(1 <<  3)
#define ZIP_UTF8	(1 << 11)

enum zip_method {
	ZIP_METHOD_STORE = 0,
	ZIP_METHOD_DEFLATE = 8
};

struct zip_local_header {
	unsigned char magic[4];
	unsigned char version[2];
	unsigned char flags[2];
	unsigned char compression_method[2];
	unsigned char mtime[2];
	unsigned char mdate[2];
	unsigned char crc32[4];
	unsigned char compressed_size[4];
	unsigned char size[4];
	unsigned char filename_length[2];
	unsigned char extra_length[2];
	unsigned char _end[1];
};

struct zip_data_desc {
	unsigned char magic[4];
	unsigned char crc32[4];
	unsigned char compressed_size[4];
	unsigned char size[4];
	unsigned char _end[1];
};

struct zip64_data_desc {
	unsigned char magic[4];
	unsigned char crc32[4];
	unsigned char compressed_size[8];
	unsigned char size[8];
	unsigned char _end[1];
};

struct zip_dir_trailer {
	unsigned char magic[4];
	unsigned char disk[2];
	unsigned char directory_start_disk[2];
	unsigned char entries_on_this_disk[2];
	unsigned char entries[2];
	unsigned char size[4];
	unsigned char offset[4];
	unsigned char comment_length[2];
	unsigned char _end[1];
};

struct zip_extra_mtime {
	unsigned char magic[2];
	unsigned char extra_size[2];
	unsigned char flags[1];
	unsigned char mtime[4];
	unsigned char _end[1];
};

struct zip64_extra {
	unsigned char magic[2];
	unsigned char extra_size[2];
	unsigned char size[8];
	unsigned char compressed_size[8];
	unsigned char _end[1];
};

struct zip64_dir_trailer {
	unsigned char magic[4];
	unsigned char record_size[8];
	unsigned char creator_version[2];
	unsigned char version[2];
	unsigned char disk[4];
	unsigned char directory_start_disk[4];
	unsigned char entries_on_this_disk[8];
	unsigned char entries[8];
	unsigned char size[8];
	unsigned char offset[8];
	unsigned char _end[1];
};

struct zip64_dir_trailer_locator {
	unsigned char magic[4];
	unsigned char disk[4];
	unsigned char offset[8];
	unsigned char number_of_disks[4];
	unsigned char _end[1];
};

/*
 * On ARM, padding is added at the end of the struct, so a simple
 * sizeof(struct ...) reports two bytes more than the payload size
 * we're interested in.
 */
#define ZIP_LOCAL_HEADER_SIZE	offsetof(struct zip_local_header, _end)
#define ZIP_DATA_DESC_SIZE	offsetof(struct zip_data_desc, _end)
#define ZIP64_DATA_DESC_SIZE	offsetof(struct zip64_data_desc, _end)
#define ZIP_DIR_HEADER_SIZE	offsetof(struct zip_dir_header, _end)
#define ZIP_DIR_TRAILER_SIZE	offsetof(struct zip_dir_trailer, _end)
#define ZIP_EXTRA_MTIME_SIZE	offsetof(struct zip_extra_mtime, _end)
#define ZIP_EXTRA_MTIME_PAYLOAD_SIZE \
	(ZIP_EXTRA_MTIME_SIZE - offsetof(struct zip_extra_mtime, flags))
#define ZIP64_EXTRA_SIZE	offsetof(struct zip64_extra, _end)
#define ZIP64_EXTRA_PAYLOAD_SIZE \
	(ZIP64_EXTRA_SIZE - offsetof(struct zip64_extra, size))
#define ZIP64_DIR_TRAILER_SIZE	offsetof(struct zip64_dir_trailer, _end)
#define ZIP64_DIR_TRAILER_RECORD_SIZE \
	(ZIP64_DIR_TRAILER_SIZE - \
	 offsetof(struct zip64_dir_trailer, creator_version))
#define ZIP64_DIR_TRAILER_LOCATOR_SIZE \
	offsetof(struct zip64_dir_trailer_locator, _end)

static void copy_le16(unsigned char *dest, unsigned int n)
{
	dest[0] = 0xff & n;
	dest[1] = 0xff & (n >> 010);
}

static void copy_le32(unsigned char *dest, unsigned int n)
{
	dest[0] = 0xff & n;
	dest[1] = 0xff & (n >> 010);
	dest[2] = 0xff & (n >> 020);
	dest[3] = 0xff & (n >> 030);
}

static void copy_le64(unsigned char *dest, uint64_t n)
{
	dest[0] = 0xff & n;
	dest[1] = 0xff & (n >> 010);
	dest[2] = 0xff & (n >> 020);
	dest[3] = 0xff & (n >> 030);
	dest[4] = 0xff & (n >> 040);
	dest[5] = 0xff & (n >> 050);
	dest[6] = 0xff & (n >> 060);
	dest[7] = 0xff & (n >> 070);
}

static uint64_t clamp_max(uint64_t n, uint64_t max, int *clamped)
{
	if (n <= max)
		return n;
	*clamped = 1;
	return max;
}

static void copy_le16_clamp(unsigned char *dest, uint64_t n, int *clamped)
{
	copy_le16(dest, clamp_max(n, 0xffff, clamped));
}

static void copy_le32_clamp(unsigned char *dest, uint64_t n, int *clamped)
{
	copy_le32(dest, clamp_max(n, 0xffffffff, clamped));
}

static int strbuf_add_le(struct strbuf *sb, size_t size, uintmax_t n)
{
	while (size-- > 0) {
		strbuf_addch(sb, n & 0xff);
		n >>= 8;
	}
	return -!!n;
}

static uint32_t clamp32(uintmax_t n)
{
	const uintmax_t max = 0xffffffff;
	return (n < max) ? n : max;
}

static void *zlib_deflate_raw(void *data, unsigned long size,
			      int compression_level,
			      unsigned long *compressed_size)
{
	git_zstream stream;
	unsigned long maxsize;
	void *buffer;
	int result;

	git_deflate_init_raw(&stream, compression_level);
	maxsize = git_deflate_bound(&stream, size);
	buffer = xmalloc(maxsize);

	stream.next_in = data;
	stream.avail_in = size;
	stream.next_out = buffer;
	stream.avail_out = maxsize;

	do {
		result = git_deflate(&stream, Z_FINISH);
	} while (result == Z_OK);

	if (result != Z_STREAM_END) {
		free(buffer);
		return NULL;
	}

	git_deflate_end(&stream);
	*compressed_size = stream.total_out;

	return buffer;
}

static void write_zip_data_desc(unsigned long size,
				unsigned long compressed_size,
				unsigned long crc)
{
	if (size >= 0xffffffff || compressed_size >= 0xffffffff) {
		struct zip64_data_desc trailer;
		copy_le32(trailer.magic, 0x08074b50);
		copy_le32(trailer.crc32, crc);
		copy_le64(trailer.compressed_size, compressed_size);
		copy_le64(trailer.size, size);
		write_or_die(1, &trailer, ZIP64_DATA_DESC_SIZE);
		zip_offset += ZIP64_DATA_DESC_SIZE;
	} else {
		struct zip_data_desc trailer;
		copy_le32(trailer.magic, 0x08074b50);
		copy_le32(trailer.crc32, crc);
		copy_le32(trailer.compressed_size, compressed_size);
		copy_le32(trailer.size, size);
		write_or_die(1, &trailer, ZIP_DATA_DESC_SIZE);
		zip_offset += ZIP_DATA_DESC_SIZE;
	}
}

static void set_zip_header_data_desc(struct zip_local_header *header,
				     unsigned long size,
				     unsigned long compressed_size,
				     unsigned long crc)
{
	copy_le32(header->crc32, crc);
	copy_le32(header->compressed_size, compressed_size);
	copy_le32(header->size, size);
}

static int has_only_ascii(const char *s)
{
	for (;;) {
		int c = *s++;
		if (c == '\0')
			return 1;
		if (!isascii(c))
			return 0;
	}
}

static int entry_is_binary(struct index_state *istate, const char *path,
			   const void *buffer, size_t size)
{
	struct userdiff_driver *driver = userdiff_find_by_path(istate, path);
	if (!driver)
		driver = userdiff_find_by_name("default");
	if (driver->binary != -1)
		return driver->binary;
	return buffer_is_binary(buffer, size);
}

#define STREAM_BUFFER_SIZE (1024 * 16)

static int write_zip_entry(struct archiver_args *args,
			   const struct object_id *oid,
			   const char *path, size_t pathlen,
			   unsigned int mode,
			   void *buffer, unsigned long size)
{
	struct zip_local_header header;
	uintmax_t offset = zip_offset;
	struct zip_extra_mtime extra;
	struct zip64_extra extra64;
	size_t header_extra_size = ZIP_EXTRA_MTIME_SIZE;
	int need_zip64_extra = 0;
	unsigned long attr2;
	unsigned long compressed_size;
	unsigned long crc;
	enum zip_method method;
	unsigned char *out;
	void *deflated = NULL;
	struct git_istream *stream = NULL;
	unsigned long flags = 0;
	int is_binary = -1;
	const char *path_without_prefix = path + args->baselen;
	unsigned int creator_version = 0;
	unsigned int version_needed = 10;
	size_t zip_dir_extra_size = ZIP_EXTRA_MTIME_SIZE;
	size_t zip64_dir_extra_payload_size = 0;

	crc = crc32(0, NULL, 0);

	if (!has_only_ascii(path)) {
		if (is_utf8(path))
			flags |= ZIP_UTF8;
		else
			warning(_("path is not valid UTF-8: %s"), path);
	}

	if (pathlen > 0xffff) {
		return error(_("path too long (%d chars, SHA1: %s): %s"),
				(int)pathlen, oid_to_hex(oid), path);
	}

	if (S_ISDIR(mode) || S_ISGITLINK(mode)) {
		method = ZIP_METHOD_STORE;
		attr2 = 16;
		out = NULL;
		compressed_size = 0;
	} else if (S_ISREG(mode) || S_ISLNK(mode)) {
		method = ZIP_METHOD_STORE;
		attr2 = S_ISLNK(mode) ? ((mode | 0777) << 16) :
			(mode & 0111) ? ((mode) << 16) : 0;
		if (S_ISLNK(mode) || (mode & 0111))
			creator_version = 0x0317;
		if (S_ISREG(mode) && args->compression_level != 0 && size > 0)
			method = ZIP_METHOD_DEFLATE;

		if (!buffer) {
			enum object_type type;
			stream = open_istream(args->repo, oid, &type, &size,
					      NULL);
			if (!stream)
				return error(_("cannot stream blob %s"),
					     oid_to_hex(oid));
			flags |= ZIP_STREAM;
			out = NULL;
		} else {
			crc = crc32(crc, buffer, size);
			is_binary = entry_is_binary(args->repo->index,
						    path_without_prefix,
						    buffer, size);
			out = buffer;
		}
		compressed_size = (method == ZIP_METHOD_STORE) ? size : 0;
	} else {
		return error(_("unsupported file mode: 0%o (SHA1: %s)"), mode,
				oid_to_hex(oid));
	}

	if (creator_version > max_creator_version)
		max_creator_version = creator_version;

	if (buffer && method == ZIP_METHOD_DEFLATE) {
		out = deflated = zlib_deflate_raw(buffer, size,
						  args->compression_level,
						  &compressed_size);
		if (!out || compressed_size >= size) {
			out = buffer;
			method = ZIP_METHOD_STORE;
			compressed_size = size;
		}
	}

	copy_le16(extra.magic, 0x5455);
	copy_le16(extra.extra_size, ZIP_EXTRA_MTIME_PAYLOAD_SIZE);
	extra.flags[0] = 1;	/* just mtime */
	copy_le32(extra.mtime, args->time);

	if (size > 0xffffffff || compressed_size > 0xffffffff)
		need_zip64_extra = 1;
	if (stream && size > 0x7fffffff)
		need_zip64_extra = 1;

	if (need_zip64_extra)
		version_needed = 45;

	copy_le32(header.magic, 0x04034b50);
	copy_le16(header.version, version_needed);
	copy_le16(header.flags, flags);
	copy_le16(header.compression_method, method);
	copy_le16(header.mtime, zip_time);
	copy_le16(header.mdate, zip_date);
	if (need_zip64_extra) {
		set_zip_header_data_desc(&header, 0xffffffff, 0xffffffff, crc);
		header_extra_size += ZIP64_EXTRA_SIZE;
	} else {
		set_zip_header_data_desc(&header, size, compressed_size, crc);
	}
	copy_le16(header.filename_length, pathlen);
	copy_le16(header.extra_length, header_extra_size);
	write_or_die(1, &header, ZIP_LOCAL_HEADER_SIZE);
	zip_offset += ZIP_LOCAL_HEADER_SIZE;
	write_or_die(1, path, pathlen);
	zip_offset += pathlen;
	write_or_die(1, &extra, ZIP_EXTRA_MTIME_SIZE);
	zip_offset += ZIP_EXTRA_MTIME_SIZE;
	if (need_zip64_extra) {
		copy_le16(extra64.magic, 0x0001);
		copy_le16(extra64.extra_size, ZIP64_EXTRA_PAYLOAD_SIZE);
		copy_le64(extra64.size, size);
		copy_le64(extra64.compressed_size, compressed_size);
		write_or_die(1, &extra64, ZIP64_EXTRA_SIZE);
		zip_offset += ZIP64_EXTRA_SIZE;
	}

	if (stream && method == ZIP_METHOD_STORE) {
		unsigned char buf[STREAM_BUFFER_SIZE];
		ssize_t readlen;

		for (;;) {
			readlen = read_istream(stream, buf, sizeof(buf));
			if (readlen <= 0)
				break;
			crc = crc32(crc, buf, readlen);
			if (is_binary == -1)
				is_binary = entry_is_binary(args->repo->index,
							    path_without_prefix,
							    buf, readlen);
			write_or_die(1, buf, readlen);
		}
		close_istream(stream);
		if (readlen)
			return readlen;

		compressed_size = size;
		zip_offset += compressed_size;

		write_zip_data_desc(size, compressed_size, crc);
	} else if (stream && method == ZIP_METHOD_DEFLATE) {
		unsigned char buf[STREAM_BUFFER_SIZE];
		ssize_t readlen;
		git_zstream zstream;
		int result;
		size_t out_len;
		unsigned char compressed[STREAM_BUFFER_SIZE * 2];

		git_deflate_init_raw(&zstream, args->compression_level);

		compressed_size = 0;
		zstream.next_out = compressed;
		zstream.avail_out = sizeof(compressed);

		for (;;) {
			readlen = read_istream(stream, buf, sizeof(buf));
			if (readlen <= 0)
				break;
			crc = crc32(crc, buf, readlen);
			if (is_binary == -1)
				is_binary = entry_is_binary(args->repo->index,
							    path_without_prefix,
							    buf, readlen);

			zstream.next_in = buf;
			zstream.avail_in = readlen;
			result = git_deflate(&zstream, 0);
			if (result != Z_OK)
				die(_("deflate error (%d)"), result);
			out_len = zstream.next_out - compressed;

			if (out_len > 0) {
				write_or_die(1, compressed, out_len);
				compressed_size += out_len;
				zstream.next_out = compressed;
				zstream.avail_out = sizeof(compressed);
			}

		}
		close_istream(stream);
		if (readlen)
			return readlen;

		zstream.next_in = buf;
		zstream.avail_in = 0;
		result = git_deflate(&zstream, Z_FINISH);
		if (result != Z_STREAM_END)
			die("deflate error (%d)", result);

		git_deflate_end(&zstream);
		out_len = zstream.next_out - compressed;
		write_or_die(1, compressed, out_len);
		compressed_size += out_len;
		zip_offset += compressed_size;

		write_zip_data_desc(size, compressed_size, crc);
	} else if (compressed_size > 0) {
		write_or_die(1, out, compressed_size);
		zip_offset += compressed_size;
	}

	free(deflated);

	if (compressed_size > 0xffffffff || size > 0xffffffff ||
	    offset > 0xffffffff) {
		if (compressed_size >= 0xffffffff)
			zip64_dir_extra_payload_size += 8;
		if (size >= 0xffffffff)
			zip64_dir_extra_payload_size += 8;
		if (offset >= 0xffffffff)
			zip64_dir_extra_payload_size += 8;
		zip_dir_extra_size += 2 + 2 + zip64_dir_extra_payload_size;
	}

	strbuf_add_le(&zip_dir, 4, 0x02014b50);	/* magic */
	strbuf_add_le(&zip_dir, 2, creator_version);
	strbuf_add_le(&zip_dir, 2, version_needed);
	strbuf_add_le(&zip_dir, 2, flags);
	strbuf_add_le(&zip_dir, 2, method);
	strbuf_add_le(&zip_dir, 2, zip_time);
	strbuf_add_le(&zip_dir, 2, zip_date);
	strbuf_add_le(&zip_dir, 4, crc);
	strbuf_add_le(&zip_dir, 4, clamp32(compressed_size));
	strbuf_add_le(&zip_dir, 4, clamp32(size));
	strbuf_add_le(&zip_dir, 2, pathlen);
	strbuf_add_le(&zip_dir, 2, zip_dir_extra_size);
	strbuf_add_le(&zip_dir, 2, 0);		/* comment length */
	strbuf_add_le(&zip_dir, 2, 0);		/* disk */
	strbuf_add_le(&zip_dir, 2, !is_binary);
	strbuf_add_le(&zip_dir, 4, attr2);
	strbuf_add_le(&zip_dir, 4, clamp32(offset));
	strbuf_add(&zip_dir, path, pathlen);
	strbuf_add(&zip_dir, &extra, ZIP_EXTRA_MTIME_SIZE);
	if (zip64_dir_extra_payload_size) {
		strbuf_add_le(&zip_dir, 2, 0x0001);	/* magic */
		strbuf_add_le(&zip_dir, 2, zip64_dir_extra_payload_size);
		if (size >= 0xffffffff)
			strbuf_add_le(&zip_dir, 8, size);
		if (compressed_size >= 0xffffffff)
			strbuf_add_le(&zip_dir, 8, compressed_size);
		if (offset >= 0xffffffff)
			strbuf_add_le(&zip_dir, 8, offset);
	}
	zip_dir_entries++;

	return 0;
}

static void write_zip64_trailer(void)
{
	struct zip64_dir_trailer trailer64;
	struct zip64_dir_trailer_locator locator64;

	copy_le32(trailer64.magic, 0x06064b50);
	copy_le64(trailer64.record_size, ZIP64_DIR_TRAILER_RECORD_SIZE);
	copy_le16(trailer64.creator_version, max_creator_version);
	copy_le16(trailer64.version, 45);
	copy_le32(trailer64.disk, 0);
	copy_le32(trailer64.directory_start_disk, 0);
	copy_le64(trailer64.entries_on_this_disk, zip_dir_entries);
	copy_le64(trailer64.entries, zip_dir_entries);
	copy_le64(trailer64.size, zip_dir.len);
	copy_le64(trailer64.offset, zip_offset);

	copy_le32(locator64.magic, 0x07064b50);
	copy_le32(locator64.disk, 0);
	copy_le64(locator64.offset, zip_offset + zip_dir.len);
	copy_le32(locator64.number_of_disks, 1);

	write_or_die(1, &trailer64, ZIP64_DIR_TRAILER_SIZE);
	write_or_die(1, &locator64, ZIP64_DIR_TRAILER_LOCATOR_SIZE);
}

static void write_zip_trailer(const struct object_id *oid)
{
	struct zip_dir_trailer trailer;
	int clamped = 0;

	copy_le32(trailer.magic, 0x06054b50);
	copy_le16(trailer.disk, 0);
	copy_le16(trailer.directory_start_disk, 0);
	copy_le16_clamp(trailer.entries_on_this_disk, zip_dir_entries,
			&clamped);
	copy_le16_clamp(trailer.entries, zip_dir_entries, &clamped);
	copy_le32(trailer.size, zip_dir.len);
	copy_le32_clamp(trailer.offset, zip_offset, &clamped);
	copy_le16(trailer.comment_length, oid ? the_hash_algo->hexsz : 0);

	write_or_die(1, zip_dir.buf, zip_dir.len);
	if (clamped)
		write_zip64_trailer();
	write_or_die(1, &trailer, ZIP_DIR_TRAILER_SIZE);
	if (oid)
		write_or_die(1, oid_to_hex(oid), the_hash_algo->hexsz);
}

static void dos_time(timestamp_t *timestamp, int *dos_date, int *dos_time)
{
	time_t time;
	struct tm tm;

	if (date_overflows(*timestamp))
		die(_("timestamp too large for this system: %"PRItime),
		    *timestamp);
	time = (time_t)*timestamp;
	localtime_r(&time, &tm);
	*timestamp = time;

	*dos_date = tm.tm_mday + (tm.tm_mon + 1) * 32 +
		    (tm.tm_year + 1900 - 1980) * 512;
	*dos_time = tm.tm_sec / 2 + tm.tm_min * 32 + tm.tm_hour * 2048;
}

static int archive_zip_config(const char *var, const char *value,
			      const struct config_context *ctx UNUSED,
			      void *data UNUSED)
{
	return userdiff_config(var, value);
}

static int write_zip_archive(const struct archiver *ar UNUSED,
			     struct archiver_args *args)
{
	int err;

	git_config(archive_zip_config, NULL);

	dos_time(&args->time, &zip_date, &zip_time);

	strbuf_init(&zip_dir, 0);

	err = write_archive_entries(args, write_zip_entry);
	if (!err)
		write_zip_trailer(args->commit_oid);

	strbuf_release(&zip_dir);

	return err;
}

static struct archiver zip_archiver = {
	.name = "zip",
	.write_archive = write_zip_archive,
	.flags = ARCHIVER_WANT_COMPRESSION_LEVELS|ARCHIVER_REMOTE,
};

void init_zip_archiver(void)
{
	register_archiver(&zip_archiver);
}
