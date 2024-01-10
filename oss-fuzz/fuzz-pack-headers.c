#include "git-compat-util.h"
#include "packfile.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	enum object_type type;
	unsigned long len;

	unpack_object_header_buffer((const unsigned char *)data,
				    (unsigned long)size, &type, &len);

	return 0;
}
