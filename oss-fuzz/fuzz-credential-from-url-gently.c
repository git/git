#include "git-compat-util.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "credential.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct credential c;
	char *buf;

	buf = malloc(size + 1);
	if (!buf)
		return 0;

	memcpy(buf, data, size);
	buf[size] = 0;

	// start fuzzing
	credential_init(&c);
	credential_from_url_gently(&c, buf, 1);

	// cleanup
	credential_clear(&c);
	free(buf);

	return 0;
}
