/*
 * Copyright (c) 2006 Rene Scharfe
 */
#include <time.h>
#include "cache.h"
#include "commit.h"
#include "blob.h"
#include "tree.h"
#include "quote.h"
#include "builtin.h"
#include "archive.h"

static int verbose;
static int zip_date;
static int zip_time;

static unsigned char *zip_dir;
static unsigned int zip_dir_size;

static unsigned int zip_offset;
static unsigned int zip_dir_offset;
static unsigned int zip_dir_entries;

#define ZIP_DIRECTORY_MIN_SIZE	(1024 * 1024)

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
};

struct zip_dir_header {
	unsigned char magic[4];
	unsigned char creator_version[2];
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
	unsigned char comment_length[2];
	unsigned char disk[2];
	unsigned char attr1[2];
	unsigned char attr2[4];
	unsigned char offset[4];
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
};

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

static void *zlib_deflate(void *data, unsigned long size,
                          unsigned long *compressed_size)
{
	z_stream stream;
	unsigned long maxsize;
	void *buffer;
	int result;

	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, zlib_compression_level);
	maxsize = deflateBound(&stream, size);
	buffer = xmalloc(maxsize);

	stream.next_in = data;
	stream.avail_in = size;
	stream.next_out = buffer;
	stream.avail_out = maxsize;

	do {
		result = deflate(&stream, Z_FINISH);
	} while (result == Z_OK);

	if (result != Z_STREAM_END) {
		free(buffer);
		return NULL;
	}

	deflateEnd(&stream);
	*compressed_size = stream.total_out;

	return buffer;
}

static char *construct_path(const char *base, int baselen,
                            const char *filename, int isdir, int *pathlen)
{
	int filenamelen = strlen(filename);
	int len = baselen + filenamelen;
	char *path, *p;

	if (isdir)
		len++;
	p = path = xmalloc(len + 1);

	memcpy(p, base, baselen);
	p += baselen;
	memcpy(p, filename, filenamelen);
	p += filenamelen;
	if (isdir)
		*p++ = '/';
	*p = '\0';

	*pathlen = len;

	return path;
}

static int write_zip_entry(const unsigned char *sha1,
                           const char *base, int baselen,
                           const char *filename, unsigned mode, int stage)
{
	struct zip_local_header header;
	struct zip_dir_header dirent;
	unsigned long attr2;
	unsigned long compressed_size;
	unsigned long uncompressed_size;
	unsigned long crc;
	unsigned long direntsize;
	unsigned long size;
	int method;
	int result = -1;
	int pathlen;
	unsigned char *out;
	char *path;
	char type[20];
	void *buffer = NULL;
	void *deflated = NULL;

	crc = crc32(0, NULL, 0);

	path = construct_path(base, baselen, filename, S_ISDIR(mode), &pathlen);
	if (verbose)
		fprintf(stderr, "%s\n", path);
	if (pathlen > 0xffff) {
		error("path too long (%d chars, SHA1: %s): %s", pathlen,
		      sha1_to_hex(sha1), path);
		goto out;
	}

	if (S_ISDIR(mode)) {
		method = 0;
		attr2 = 16;
		result = READ_TREE_RECURSIVE;
		out = NULL;
		uncompressed_size = 0;
		compressed_size = 0;
	} else if (S_ISREG(mode) || S_ISLNK(mode)) {
		method = 0;
		attr2 = S_ISLNK(mode) ? ((mode | 0777) << 16) : 0;
		if (S_ISREG(mode) && zlib_compression_level != 0)
			method = 8;
		result = 0;
		buffer = read_sha1_file(sha1, type, &size);
		if (!buffer)
			die("cannot read %s", sha1_to_hex(sha1));
		crc = crc32(crc, buffer, size);
		out = buffer;
		uncompressed_size = size;
		compressed_size = size;
	} else {
		error("unsupported file mode: 0%o (SHA1: %s)", mode,
		      sha1_to_hex(sha1));
		goto out;
	}

	if (method == 8) {
		deflated = zlib_deflate(buffer, size, &compressed_size);
		if (deflated && compressed_size - 6 < size) {
			/* ZLIB --> raw compressed data (see RFC 1950) */
			/* CMF and FLG ... */
			out = (unsigned char *)deflated + 2;
			compressed_size -= 6;	/* ... and ADLER32 */
		} else {
			method = 0;
			compressed_size = size;
		}
	}

	/* make sure we have enough free space in the dictionary */
	direntsize = sizeof(struct zip_dir_header) + pathlen;
	while (zip_dir_size < zip_dir_offset + direntsize) {
		zip_dir_size += ZIP_DIRECTORY_MIN_SIZE;
		zip_dir = xrealloc(zip_dir, zip_dir_size);
	}

	copy_le32(dirent.magic, 0x02014b50);
	copy_le16(dirent.creator_version, S_ISLNK(mode) ? 0x0317 : 0);
	copy_le16(dirent.version, 10);
	copy_le16(dirent.flags, 0);
	copy_le16(dirent.compression_method, method);
	copy_le16(dirent.mtime, zip_time);
	copy_le16(dirent.mdate, zip_date);
	copy_le32(dirent.crc32, crc);
	copy_le32(dirent.compressed_size, compressed_size);
	copy_le32(dirent.size, uncompressed_size);
	copy_le16(dirent.filename_length, pathlen);
	copy_le16(dirent.extra_length, 0);
	copy_le16(dirent.comment_length, 0);
	copy_le16(dirent.disk, 0);
	copy_le16(dirent.attr1, 0);
	copy_le32(dirent.attr2, attr2);
	copy_le32(dirent.offset, zip_offset);
	memcpy(zip_dir + zip_dir_offset, &dirent, sizeof(struct zip_dir_header));
	zip_dir_offset += sizeof(struct zip_dir_header);
	memcpy(zip_dir + zip_dir_offset, path, pathlen);
	zip_dir_offset += pathlen;
	zip_dir_entries++;

	copy_le32(header.magic, 0x04034b50);
	copy_le16(header.version, 10);
	copy_le16(header.flags, 0);
	copy_le16(header.compression_method, method);
	copy_le16(header.mtime, zip_time);
	copy_le16(header.mdate, zip_date);
	copy_le32(header.crc32, crc);
	copy_le32(header.compressed_size, compressed_size);
	copy_le32(header.size, uncompressed_size);
	copy_le16(header.filename_length, pathlen);
	copy_le16(header.extra_length, 0);
	write_or_die(1, &header, sizeof(struct zip_local_header));
	zip_offset += sizeof(struct zip_local_header);
	write_or_die(1, path, pathlen);
	zip_offset += pathlen;
	if (compressed_size > 0) {
		write_or_die(1, out, compressed_size);
		zip_offset += compressed_size;
	}

out:
	free(buffer);
	free(deflated);
	free(path);

	return result;
}

static void write_zip_trailer(const unsigned char *sha1)
{
	struct zip_dir_trailer trailer;

	copy_le32(trailer.magic, 0x06054b50);
	copy_le16(trailer.disk, 0);
	copy_le16(trailer.directory_start_disk, 0);
	copy_le16(trailer.entries_on_this_disk, zip_dir_entries);
	copy_le16(trailer.entries, zip_dir_entries);
	copy_le32(trailer.size, zip_dir_offset);
	copy_le32(trailer.offset, zip_offset);
	copy_le16(trailer.comment_length, sha1 ? 40 : 0);

	write_or_die(1, zip_dir, zip_dir_offset);
	write_or_die(1, &trailer, sizeof(struct zip_dir_trailer));
	if (sha1)
		write_or_die(1, sha1_to_hex(sha1), 40);
}

static void dos_time(time_t *time, int *dos_date, int *dos_time)
{
	struct tm *t = localtime(time);

	*dos_date = t->tm_mday + (t->tm_mon + 1) * 32 +
	            (t->tm_year + 1900 - 1980) * 512;
	*dos_time = t->tm_sec / 2 + t->tm_min * 32 + t->tm_hour * 2048;
}

int write_zip_archive(struct archiver_args *args)
{
	int plen = strlen(args->base);

	dos_time(&args->time, &zip_date, &zip_time);

	zip_dir = xmalloc(ZIP_DIRECTORY_MIN_SIZE);
	zip_dir_size = ZIP_DIRECTORY_MIN_SIZE;
	verbose = args->verbose;

	if (args->base && plen > 0 && args->base[plen - 1] == '/') {
		char *base = xstrdup(args->base);
		int baselen = strlen(base);

		while (baselen > 0 && base[baselen - 1] == '/')
			base[--baselen] = '\0';
		write_zip_entry(args->tree->object.sha1, "", 0, base, 040777, 0);
		free(base);
	}
	read_tree_recursive(args->tree, args->base, plen, 0,
			    args->pathspec, write_zip_entry);
	write_zip_trailer(args->commit_sha1);

	free(zip_dir);

	return 0;
}

void *parse_extra_zip_args(int argc, const char **argv)
{
	for (; argc > 0; argc--, argv++) {
		const char *arg = argv[0];

		if (arg[0] == '-' && isdigit(arg[1]) && arg[2] == '\0')
			zlib_compression_level = arg[1] - '0';
		else
			die("Unknown argument for zip format: %s", arg);
	}
	return NULL;
}
