#ifndef DATE_H
#define DATE_H

/**
 * The date mode type. This has DATE_NORMAL at an explicit "= 0" to
 * accommodate a memset([...], 0, [...]) initialization when "struct
 * date_mode" is used as an embedded struct member, as in the case of
 * e.g. "struct pretty_print_context" and "struct rev_info".
 */
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
	int local;
	const char *strftime_fmt;
};

#define DATE_MODE_INIT { \
	.type = DATE_NORMAL, \
}

/**
 * Convenience helper for passing a constant type, like:
 *
 *   show_date(t, tz, DATE_MODE(NORMAL));
 */
#define DATE_MODE(t) date_mode_from_type(DATE_##t)
struct date_mode date_mode_from_type(enum date_mode_type type);

/**
 * Format <'time', 'timezone'> into static memory according to 'mode'
 * and return it. The mode is an initialized "struct date_mode"
 * (usually from the DATE_MODE() macro).
 */
const char *show_date(timestamp_t time, int timezone, struct date_mode mode);

/**
 * Parse a date format for later use with show_date().
 *
 * When the "date_mode_type" is DATE_STRFTIME the "strftime_fmt"
 * member of "struct date_mode" will be a malloc()'d format string to
 * be used with strbuf_addftime(), in which case you'll need to call
 * date_mode_release() later.
 */
void parse_date_format(const char *format, struct date_mode *mode);

/**
 * Release a "struct date_mode", currently only required if
 * parse_date_format() has parsed a "DATE_STRFTIME" format.
 */
void date_mode_release(struct date_mode *mode);

void show_date_relative(timestamp_t time, struct strbuf *timebuf);
int parse_date(const char *date, struct strbuf *out);
int parse_date_basic(const char *date, timestamp_t *timestamp, int *offset);
int parse_expiry_date(const char *date, timestamp_t *timestamp);
void datestamp(struct strbuf *out);
#define approxidate(s) approxidate_careful((s), NULL)
timestamp_t approxidate_careful(const char *, int *);
int date_overflows(timestamp_t date);
time_t tm_to_time_t(const struct tm *tm);
#endif
