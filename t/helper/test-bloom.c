#include "git-compat-util.h"
#include "bloom.h"
#include "test-tool.h"

int cmd__bloom(int argc, const char **argv)
{
	if (!strcmp(argv[1], "get_murmur3")) {
		uint32_t hashed = murmur3_seeded(0, argv[2], strlen(argv[2]));
		printf("Murmur3 Hash with seed=0:0x%08x\n", hashed);
	}

	return 0;
}