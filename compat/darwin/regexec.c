#include "git-compat-util.h"

#include <wchar.h>

/*
 * Darwin's TRE regex engine leaks an internal buffer when it encounters an
 * invalid multibyte sequence.  Since the leak has already happened when
 * regexec() reports REG_ILLSEQ, keep invalid bytes out of regexec() by
 * searching each valid segment separately.
 */

/*
 * Search buf[start, end), where size is the full size of buf.  REG_STARTEND
 * keeps match offsets relative to buf.  Do not let an internal segment create
 * a false beginning or end of line.
 */
static int regexec_segment(const regex_t *preg, const char *buf,
			   size_t size, size_t start, size_t end,
			   size_t nmatch, regmatch_t pmatch[], int eflags)
{
	eflags |= REG_STARTEND;
	if (start > 0)
		eflags |= REG_NOTBOL;
	if (end < size)
		eflags |= REG_NOTEOL;
	pmatch[0].rm_so = start;
	pmatch[0].rm_eo = end;
	return regexec(preg, buf, nmatch, pmatch, eflags);
}

int darwin_regexec_buf(const regex_t *preg, const char *buf, size_t size,
		       size_t nmatch, regmatch_t pmatch[], int eflags)
{
	size_t seg_start = 0, i = 0;
	mbstate_t mbs;

	assert(nmatch > 0 && pmatch);

	/*
	 * A single-byte locale cannot contain an invalid multibyte sequence,
	 * so use regexec() directly.
	 */
	if (MB_CUR_MAX == 1) {
		pmatch[0].rm_so = 0;
		pmatch[0].rm_eo = size;
		return regexec(preg, buf, nmatch, pmatch, eflags | REG_STARTEND);
	}

	memset(&mbs, 0, sizeof(mbs));
	while (i < size) {
		unsigned char c = (unsigned char)buf[i];
		size_t n;

		if (c < 0x80) {
			i++;
			continue;
		}

		n = mbrtowc(NULL, buf + i, size - i, &mbs);
		if (!n)
			n = 1;
		if (n != (size_t)-1 && n != (size_t)-2) {
			i += n;
			continue;
		}

		/*
		 * -1 denotes an encoding error; -2 denotes an incomplete
		 * trailing sequence.  In either case, buf[i] cannot begin a
		 * complete valid character within this buffer.  Search an
		 * empty initial segment to preserve zero-width matches at the
		 * true beginning.
		 */
		if (i > seg_start || i == 0) {
			int ret = regexec_segment(preg, buf, size, seg_start, i,
						  nmatch, pmatch, eflags);
			if (ret != REG_NOMATCH)
				return ret;
		}
		i++;
		seg_start = i;
		memset(&mbs, 0, sizeof(mbs));
	}

	/*
	 * Search the final segment even when it is empty, so an empty buffer
	 * or a buffer ending in invalid bytes still has its true end.
	 */
	return regexec_segment(preg, buf, size, seg_start, size,
			       nmatch, pmatch, eflags);
}
