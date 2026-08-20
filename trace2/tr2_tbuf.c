#include "git-compat-util.h"
#include "tr2_tbuf.h"

void tr2_tbuf_local_time(struct tr2_tbuf *tb)
{
	struct timeval tv = { 0 };
	struct tm tm = { 0 };
	time_t secs;
	int len;

	gettimeofday(&tv, NULL);
	secs = tv.tv_sec;
	localtime_r(&secs, &tm);

	len = snprintf(tb->buf, sizeof(tb->buf), "%02d:%02d:%02d.%06ld",
		       tm.tm_hour, tm.tm_min, tm.tm_sec, (long)tv.tv_usec);

	if (len < 0 || (size_t)len >= sizeof(tb->buf)) {
		const char *blank = "00:00:00.000000";
		strlcpy(tb->buf, blank, sizeof(tb->buf));
	}
}

void tr2_tbuf_utc_datetime_extended(struct tr2_tbuf *tb)
{
	struct timeval tv = { 0 };
	struct tm tm = { 0 };
	time_t secs;
	int len;

	gettimeofday(&tv, NULL);
	secs = tv.tv_sec;
	gmtime_r(&secs, &tm);

	len = snprintf(tb->buf, sizeof(tb->buf),
		       "%4d-%02d-%02dT%02d:%02d:%02d.%06ldZ",
		       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		       tm.tm_hour, tm.tm_min, tm.tm_sec, (long)tv.tv_usec);

	if (len < 0 || (size_t)len >= sizeof(tb->buf)) {
		const char *blank = "1900-00-00T00:00:00.000000Z";
		strlcpy(tb->buf, blank, sizeof(tb->buf));
	}
}

void tr2_tbuf_utc_datetime(struct tr2_tbuf *tb)
{
	struct timeval tv = { 0 };
	struct tm tm = { 0 };
	time_t secs;
	int len;

	gettimeofday(&tv, NULL);
	secs = tv.tv_sec;
	gmtime_r(&secs, &tm);

	len = snprintf(tb->buf, sizeof(tb->buf),
		       "%4d%02d%02dT%02d%02d%02d.%06ldZ",
		       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		       tm.tm_hour, tm.tm_min, tm.tm_sec, (long)tv.tv_usec);

	if (len < 0 || (size_t)len >= sizeof(tb->buf)) {
		const char *blank = "19000000T000000.000000Z";
		strlcpy(tb->buf, blank, sizeof(tb->buf));
	}
}
