#include "git-compat-util.h"
#include "date.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	int local;
	int num;
	char *str;
	int16_t tz;
	timestamp_t ts;
	enum date_mode_type dmtype;
	struct date_mode dm;

	if (size <= 4)
		/*
		 * we use the first byte to fuzz dmtype and the
		 * second byte to fuzz local, then the next two
		 * bytes to fuzz tz offset. The remainder
		 * (at least one byte) is fed as input to
		 * approxidate_careful().
		 */
		return 0;

	local = !!(*data++ & 0x10);
	num = *data++ % DATE_UNIX;
	if (num >= DATE_STRFTIME)
		num++;
	dmtype = (enum date_mode_type)num;
	size -= 2;

	tz = *data++;
	tz = (tz << 8) | *data++;
	size -= 2;

	str = xmemdupz(data, size);

	ts = approxidate_careful(str, &num);
	free(str);

	dm = date_mode_from_type(dmtype);
	dm.local = local;
	show_date(ts, (int)tz, dm);

	date_mode_release(&dm);

	return 0;
}
