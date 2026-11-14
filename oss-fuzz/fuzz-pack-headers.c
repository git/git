#include "git-compat-util.h"
#include "packfile.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	enum object_type type;
	size_t len;

	unpack_object_header_buffer((const unsigned char *)data,
				    size, &type, &len);

	return 0;
}
