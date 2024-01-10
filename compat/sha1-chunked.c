#include "git-compat-util.h"
#include "hash-ll.h"

int git_SHA1_Update_Chunked(platform_SHA_CTX *c, const void *data, size_t len)
{
	size_t nr;
	size_t total = 0;
	const char *cdata = (const char*)data;

	while (len) {
		nr = len;
		if (nr > SHA1_MAX_BLOCK_SIZE)
			nr = SHA1_MAX_BLOCK_SIZE;
		platform_SHA1_Update(c, cdata, nr);
		total += nr;
		cdata += nr;
		len -= nr;
	}
	return total;
}
