/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 *
 * This handles basic git sha1 object files - packing, unpacking,
 * creation etc.
 */
#include <stdarg.h>
#include "cache.h"

#ifndef O_NOATIME
#if defined(__linux__) && (defined(__i386__) || defined(__PPC__))
#define O_NOATIME 01000000
#else
#define O_NOATIME 0
#endif
#endif

static unsigned int sha1_file_open_flag = O_NOATIME;

static unsigned hexval(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return ~0;
}

int get_sha1_hex(const char *hex, unsigned char *sha1)
{
	int i;
	for (i = 0; i < 20; i++) {
		unsigned int val = (hexval(hex[0]) << 4) | hexval(hex[1]);
		if (val & ~0xff)
			return -1;
		*sha1++ = val;
		hex += 2;
	}
	return 0;
}

int get_sha1_file(const char *path, unsigned char *result)
{
	char buffer[60];
	int fd = open(path, O_RDONLY);
	int len;

	if (fd < 0)
		return -1;
	len = read(fd, buffer, sizeof(buffer));
	close(fd);
	if (len < 40)
		return -1;
	return get_sha1_hex(buffer, result);
}

int get_sha1(const char *str, unsigned char *sha1)
{
	static char pathname[PATH_MAX];
	static const char *prefix[] = {
		"",
		"refs",
		"refs/tags",
		"refs/heads",
		"refs/snap",
		NULL
	};
	const char *gitdir;
	const char **p;

	if (!get_sha1_hex(str, sha1))
		return 0;

	gitdir = ".git";
	for (p = prefix; *p; p++) {
		snprintf(pathname, sizeof(pathname), "%s/%s/%s", gitdir, *p, str);
		if (!get_sha1_file(pathname, sha1))
			return 0;
	}

	return -1;
}

char * sha1_to_hex(const unsigned char *sha1)
{
	static char buffer[50];
	static const char hex[] = "0123456789abcdef";
	char *buf = buffer;
	int i;

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	return buffer;
}

/*
 * NOTE! This returns a statically allocated buffer, so you have to be
 * careful about using it. Do a "strdup()" if you need to save the
 * filename.
 */
char *sha1_file_name(const unsigned char *sha1)
{
	int i;
	static char *name, *base;

	if (!base) {
		char *sha1_file_directory = getenv(DB_ENVIRONMENT) ? : DEFAULT_DB_ENVIRONMENT;
		int len = strlen(sha1_file_directory);
		base = xmalloc(len + 60);
		memcpy(base, sha1_file_directory, len);
		memset(base+len, 0, 60);
		base[len] = '/';
		base[len+3] = '/';
		name = base + len + 1;
	}
	for (i = 0; i < 20; i++) {
		static char hex[] = "0123456789abcdef";
		unsigned int val = sha1[i];
		char *pos = name + i*2 + (i > 0);
		*pos++ = hex[val >> 4];
		*pos = hex[val & 0xf];
	}
	return base;
}

int check_sha1_signature(unsigned char *sha1, void *map, unsigned long size, const char *type)
{
	char header[100];
	unsigned char real_sha1[20];
	SHA_CTX c;

	SHA1_Init(&c);
	SHA1_Update(&c, header, 1+sprintf(header, "%s %lu", type, size));
	SHA1_Update(&c, map, size);
	SHA1_Final(real_sha1, &c);
	return memcmp(sha1, real_sha1, 20) ? -1 : 0;
}

void *map_sha1_file(const unsigned char *sha1, unsigned long *size)
{
	char *filename = sha1_file_name(sha1);
	struct stat st;
	void *map;
	int fd;

	fd = open(filename, O_RDONLY | sha1_file_open_flag);
	if (fd < 0) {
		/* See if it works without O_NOATIME */
		switch (sha1_file_open_flag) {
		default:
			fd = open(filename, O_RDONLY);
			if (fd >= 0)
				break;
		/* Fallthrough */
		case 0:
			perror(filename);
			return NULL;
		}

		/* If it failed once, it will probably fail again. Stop using O_NOATIME */
		sha1_file_open_flag = 0;
	}
	if (fstat(fd, &st) < 0) {
		close(fd);
		return NULL;
	}
	map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (-1 == (int)(long)map)
		return NULL;
	*size = st.st_size;
	return map;
}

void * unpack_sha1_file(void *map, unsigned long mapsize, char *type, unsigned long *size)
{
	int ret, bytes;
	z_stream stream;
	char buffer[8192];
	char *buf;

	/* Get the data stream */
	memset(&stream, 0, sizeof(stream));
	stream.next_in = map;
	stream.avail_in = mapsize;
	stream.next_out = buffer;
	stream.avail_out = sizeof(buffer);

	inflateInit(&stream);
	ret = inflate(&stream, 0);
	if (ret < Z_OK)
		return NULL;
	if (sscanf(buffer, "%10s %lu", type, size) != 2)
		return NULL;

	bytes = strlen(buffer) + 1;
	buf = xmalloc(*size);

	memcpy(buf, buffer + bytes, stream.total_out - bytes);
	bytes = stream.total_out - bytes;
	if (bytes < *size && ret == Z_OK) {
		stream.next_out = buf + bytes;
		stream.avail_out = *size - bytes;
		while (inflate(&stream, Z_FINISH) == Z_OK)
			/* nothing */;
	}
	inflateEnd(&stream);
	return buf;
}

void * read_sha1_file(const unsigned char *sha1, char *type, unsigned long *size)
{
	unsigned long mapsize;
	void *map, *buf;

	map = map_sha1_file(sha1, &mapsize);
	if (map) {
		buf = unpack_sha1_file(map, mapsize, type, size);
		munmap(map, mapsize);
		return buf;
	}
	return NULL;
}

void *read_object_with_reference(const unsigned char *sha1,
				 const unsigned char *required_type,
				 unsigned long *size,
				 unsigned char *actual_sha1_return)
{
	char type[20];
	void *buffer;
	unsigned long isize;
	unsigned char actual_sha1[20];

	memcpy(actual_sha1, sha1, 20);
	while (1) {
		int ref_length = -1;
		const char *ref_type = NULL;

		buffer = read_sha1_file(actual_sha1, type, &isize);
		if (!buffer)
			return NULL;
		if (!strcmp(type, required_type)) {
			*size = isize;
			if (actual_sha1_return)
				memcpy(actual_sha1_return, actual_sha1, 20);
			return buffer;
		}
		/* Handle references */
		else if (!strcmp(type, "commit"))
			ref_type = "tree ";
		else if (!strcmp(type, "tag"))
			ref_type = "object ";
		else {
			free(buffer);
			return NULL;
		}
		ref_length = strlen(ref_type);

		if (memcmp(buffer, ref_type, ref_length) ||
		    get_sha1_hex(buffer + ref_length, actual_sha1)) {
			free(buffer);
			return NULL;
		}
		/* Now we have the ID of the referred-to object in
		 * actual_sha1.  Check again. */
	}
}

int write_sha1_file(char *buf, unsigned long len, const char *type, unsigned char *returnsha1)
{
	int size;
	char *compressed;
	z_stream stream;
	unsigned char sha1[20];
	SHA_CTX c;
	char *filename;
	static char tmpfile[PATH_MAX];
	char hdr[50];
	int fd, hdrlen, ret;

	/* Generate the header */
	hdrlen = sprintf(hdr, "%s %lu", type, len)+1;

	/* Sha1.. */
	SHA1_Init(&c);
	SHA1_Update(&c, hdr, hdrlen);
	SHA1_Update(&c, buf, len);
	SHA1_Final(sha1, &c);

	if (returnsha1)
		memcpy(returnsha1, sha1, 20);

	filename = sha1_file_name(sha1);
	fd = open(filename, O_RDONLY);
	if (fd >= 0) {
		/*
		 * FIXME!!! We might do collision checking here, but we'd
		 * need to uncompress the old file and check it. Later.
		 */
		close(fd);
		return 0;
	}

	if (errno != ENOENT) {
		fprintf(stderr, "sha1 file %s: %s", filename, strerror(errno));
		return -1;
	}

	snprintf(tmpfile, sizeof(tmpfile), "%s/obj_XXXXXX", get_object_directory());
	fd = mkstemp(tmpfile);
	if (fd < 0) {
		fprintf(stderr, "unable to create temporary sha1 filename %s: %s", tmpfile, strerror(errno));
		return -1;
	}

	/* Set it up */
	memset(&stream, 0, sizeof(stream));
	deflateInit(&stream, Z_BEST_COMPRESSION);
	size = deflateBound(&stream, len+hdrlen);
	compressed = xmalloc(size);

	/* Compress it */
	stream.next_out = compressed;
	stream.avail_out = size;

	/* First header.. */
	stream.next_in = hdr;
	stream.avail_in = hdrlen;
	while (deflate(&stream, 0) == Z_OK)
		/* nothing */

	/* Then the data itself.. */
	stream.next_in = buf;
	stream.avail_in = len;
	while (deflate(&stream, Z_FINISH) == Z_OK)
		/* nothing */;
	deflateEnd(&stream);
	size = stream.total_out;

	if (write(fd, compressed, size) != size)
		die("unable to write file");
	fchmod(fd, 0444);
	close(fd);

	ret = link(tmpfile, filename);
	if (ret < 0) {
		ret = errno;

		/*
		 * Coda hack - coda doesn't like cross-directory links,
		 * so we fall back to a rename, which will mean that it
		 * won't be able to check collisions, but that's not a
		 * big deal.
		 *
		 * When this succeeds, we just return 0. We have nothing
		 * left to unlink.
		 */
		if (ret == EXDEV && !rename(tmpfile, filename))
			return 0;
	}
	unlink(tmpfile);
	if (ret) {
		if (ret != EEXIST) {
			fprintf(stderr, "unable to write sha1 filename %s: %s", filename, strerror(ret));
			return -1;
		}
		/* FIXME!!! Collision check here ? */
	}

	return 0;
}

int write_sha1_from_fd(const unsigned char *sha1, int fd)
{
	char *filename = sha1_file_name(sha1);

	int local;
	z_stream stream;
	unsigned char real_sha1[20];
	char buf[4096];
	char discard[4096];
	int ret;
	SHA_CTX c;

	local = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0666);

	if (local < 0)
		return error("Couldn't open %s\n", filename);

	memset(&stream, 0, sizeof(stream));

	inflateInit(&stream);

	SHA1_Init(&c);

	do {
		ssize_t size;
		size = read(fd, buf, 4096);
		if (size <= 0) {
			close(local);
			unlink(filename);
			if (!size)
				return error("Connection closed?");
			perror("Reading from connection");
			return -1;
		}
		write(local, buf, size);
		stream.avail_in = size;
		stream.next_in = buf;
		do {
			stream.next_out = discard;
			stream.avail_out = sizeof(discard);
			ret = inflate(&stream, Z_SYNC_FLUSH);
			SHA1_Update(&c, discard, sizeof(discard) -
				    stream.avail_out);
		} while (stream.avail_in && ret == Z_OK);
		
	} while (ret == Z_OK);
	inflateEnd(&stream);

	close(local);
	SHA1_Final(real_sha1, &c);
	if (ret != Z_STREAM_END) {
		unlink(filename);
		return error("File %s corrupted", sha1_to_hex(sha1));
	}
	if (memcmp(sha1, real_sha1, 20)) {
		unlink(filename);
		return error("File %s has bad hash\n", sha1_to_hex(sha1));
	}
	
	return 0;
}

int has_sha1_file(const unsigned char *sha1)
{
	char *filename = sha1_file_name(sha1);
	struct stat st;

	if (!stat(filename, &st))
		return 1;
	return 0;
}

int index_fd(unsigned char *sha1, int fd, struct stat *st)
{
	unsigned long size = st->st_size;
	void *buf;
	int ret;

	buf = "";
	if (size)
		buf = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if ((int)(long)buf == -1)
		return -1;

	ret = write_sha1_file(buf, size, "blob", sha1);
	if (size)
		munmap(buf, size);
	return ret;
}
