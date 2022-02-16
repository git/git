#ifndef DATE_H
#define DATE_H

enum date_mode_type {
	DATE_NORMAL = 0,
	DATE_HUMAN,
	DATE_RELATIVE,
	DATE_SHORT,
	DATE_ISO8601,
	DATE_ISO8601_STRICT,
	DATE_RFC2822,
	DATE_STRFTIME,
	DATE_RAW,
	DATE_UNIX
};

struct date_mode {
	enum date_mode_type type;
	const char *strftime_fmt;
	int local;
};

#define DATE_MODE_INIT { \
	.type = DATE_NORMAL, \
}

/*
 * Convenience helper for passing a constant type, like:
 *
 *   show_date(t, tz, DATE_MODE(NORMAL));
 */
#define DATE_MODE(t) date_mode_from_type(DATE_##t)
struct date_mode *date_mode_from_type(enum date_mode_type type);

const char *show_date(timestamp_t time, int timezone, const struct date_mode *mode);
void show_date_relative(timestamp_t time, struct strbuf *timebuf);
int parse_date(const char *date, struct strbuf *out);
int parse_date_basic(const char *date, timestamp_t *timestamp, int *offset);
int parse_expiry_date(const char *date, timestamp_t *timestamp);
void datestamp(struct strbuf *out);
#define approxidate(s) approxidate_careful((s), NULL)
timestamp_t approxidate_careful(const char *, int *);
timestamp_t approxidate_relative(const char *date);
void parse_date_format(const char *format, struct date_mode *mode);
int date_overflows(timestamp_t date);
time_t tm_to_time_t(const struct tm *tm);
#endif
