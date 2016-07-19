#include "cache.h"

int cmd_main(int argc, const char **argv)
{
	struct cache_header hdr;
	int version;

	memset(&hdr,0,sizeof(hdr));
	if (read(0, &hdr, sizeof(hdr)) != sizeof(hdr))
		return 0;
	version = ntohl(hdr.hdr_version);
	printf("%d\n", version);
	return 0;
}
