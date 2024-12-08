#include "git-compat-util.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "url.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char *buf;
	char *r;
	const char *pbuf;

	buf = malloc(size + 1);
	if (!buf)
		return 0;

	memcpy(buf, data, size);
	buf[size] = 0;

	// start fuzzing
	r = url_decode(buf);
	free(r);

	r = url_percent_decode(buf);
	free(r);

	pbuf = (const char*) buf;
	r = url_decode_parameter_name(&pbuf);
	free(r);

	pbuf = (const char*) buf;
	r = url_decode_parameter_value(&pbuf);
	free(r);

	// cleanup
	free(buf);

	return 0;
}
