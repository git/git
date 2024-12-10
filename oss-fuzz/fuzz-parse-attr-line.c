#include "git-compat-util.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "attr.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct match_attr *res;
	char *buf;

	buf = malloc(size + 1);
	if (!buf)
		return 0;

	memcpy(buf, data, size);
	buf[size] = 0;

	res = parse_attr_line(buf, "dummy", 0, 0);

	if (res) {
		int j;
		for (j = 0; j < res->num_attr; j++) {
			const char *setto = res->state[j].setto;
			if (ATTR_TRUE(setto) || ATTR_FALSE(setto) ||
				ATTR_UNSET(setto))
				;
			else
				free((char *)setto);
		}
		free(res);
	}
	free(buf);

	return 0;
}
