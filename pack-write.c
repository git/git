#include "cache.h"
#include "pack.h"

void fixup_pack_header_footer(int pack_fd,
			 unsigned char *pack_file_sha1,
			 const char *pack_name,
			 uint32_t object_count)
{
	static const int buf_sz = 128 * 1024;
	SHA_CTX c;
	struct pack_header hdr;
	char *buf;

	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die("Failed seeking to start: %s", strerror(errno));
	if (read_in_full(pack_fd, &hdr, sizeof(hdr)) != sizeof(hdr))
		die("Unable to reread header of %s: %s", pack_name, strerror(errno));
	if (lseek(pack_fd, 0, SEEK_SET) != 0)
		die("Failed seeking to start: %s", strerror(errno));
	hdr.hdr_entries = htonl(object_count);
	write_or_die(pack_fd, &hdr, sizeof(hdr));

	SHA1_Init(&c);
	SHA1_Update(&c, &hdr, sizeof(hdr));

	buf = xmalloc(buf_sz);
	for (;;) {
		ssize_t n = xread(pack_fd, buf, buf_sz);
		if (!n)
			break;
		if (n < 0)
			die("Failed to checksum %s: %s", pack_name, strerror(errno));
		SHA1_Update(&c, buf, n);
	}
	free(buf);

	SHA1_Final(pack_file_sha1, &c);
	write_or_die(pack_fd, pack_file_sha1, 20);
}
