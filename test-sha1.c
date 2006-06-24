#include "cache.h"

int main(int ac, char **av)
{
	SHA_CTX ctx;
	unsigned char sha1[20];

	SHA1_Init(&ctx);

	while (1) {
		ssize_t sz;
		char buffer[8192];
		sz = xread(0, buffer, sizeof(buffer));
		if (sz == 0)
			break;
		if (sz < 0)
			die("test-sha1: %s", strerror(errno));
		SHA1_Update(&ctx, buffer, sz);
	}
	SHA1_Final(sha1, &ctx);
	puts(sha1_to_hex(sha1));
	exit(0);
}

