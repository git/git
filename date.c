/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */

#include "cache.h"

/*
 * This is like mktime, but without normalization of tm_wday and tm_yday.
 */
static time_t tm_to_time_t(const struct tm *tm)
{
	static const int mdays[] = {
	    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
	};
	int year = tm->tm_year - 70;
	int month = tm->tm_mon;
	int day = tm->tm_mday;

	if (year < 0 || year > 129) /* algo only works for 1970-2099 */
		return -1;
	if (month < 0 || month > 11) /* array bounds */
		return -1;
	if (month < 2 || (year + 2) % 4)
		day--;
	if (tm->tm_hour < 0 || tm->tm_min < 0 || tm->tm_sec < 0)
		return -1;
	return (year * 365 + (year + 1) / 4 + mdays[month] + day) * 24*60*60UL +
		tm->tm_hour * 60*60 + tm->tm_min * 60 + tm->tm_sec;
}

static const char *month_names[] = {
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
};

static const char *weekday_names[] = {
	"Sundays", "Mondays", "Tuesdays", "Wednesdays", "Thursdays", "Fridays", "Saturdays"
};

static time_t gm_time_t(unsigned long time, int tz)
{
	int minutes;

	minutes = tz < 0 ? -tz : tz;
	minutes = (minutes / 100)*60 + (minutes % 100);
	minutes = tz < 0 ? -minutes : minutes;
	return time + minutes * 60;
}

/*
 * The "tz" thing is passed in as this strange "decimal parse of tz"
 * thing, which means that tz -0100 is passed in as the integer -100,
 * even though it means "sixty minutes off"
 */
static struct tm *time_to_tm(unsigned long time, int tz)
{
	time_t t = gm_time_t(time, tz);
	return gmtime(&t);
}

/*
 * What value of "tz" was in effect back then at "time" in the
 * local timezone?
 */
static int local_tzoffset(unsigned long time)
{
	time_t t, t_local;
	struct tm tm;
	int offset, eastwest;

	t = time;
	localtime_r(&t, &tm);
	t_local = tm_to_time_t(&tm);

	if (t_local < t) {
		eastwest = -1;
		offset = t - t_local;
	} else {
		eastwest = 1;
		offset = t_local - t;
	}
	offset /= 60; /* in minutes */
	offset = (offset % 60) + ((offset / 60) * 100);
	return offset * eastwest;
}

const char *show_date_relative(unsigned long time, int tz,
			       const struct timeval *now,
			       char *timebuf,
			       size_t timebuf_size)
{
	unsigned long diff;
	if (now->tv_sec < time)
		return "in the future";
	diff = now->tv_sec - time;
	if (diff < 90) {
		snprintf(timebuf, timebuf_size, "%lu seconds ago", diff);
		return timebuf;
	}
	/* Turn it into minutes */
	diff = (diff + 30) / 60;
	if (diff < 90) {
		snprintf(timebuf, timebuf_size, "%lu minutes ago", diff);
		return timebuf;
	}
	/* Turn it into hours */
	diff = (diff + 30) / 60;
	if (diff < 36) {
		snprintf(timebuf, timebuf_size, "%lu hours ago", diff);
		return timebuf;
	}
	/* We deal with number of days from here on */
	diff = (diff + 12) / 24;
	if (diff < 14) {
		snprintf(timebuf, timebuf_size, "%lu days ago", diff);
		return timebuf;
	}
	/* Say weeks for the past 10 weeks or so */
	if (diff < 70) {
		snprintf(timebuf, timebuf_size, "%lu weeks ago", (diff + 3) / 7);
		return timebuf;
	}
	/* Say months for the past 12 months or so */
	if (diff < 365) {
		snprintf(timebuf, timebuf_size, "%lu months ago", (diff + 15) / 30);
		return timebuf;
	}
	/* Give years and months for 5 years or so */
	if (diff < 1825) {
		unsigned long years = diff / 365;
		unsigned long months = (diff % 365 + 15) / 30;
		int n;
		n = snprintf(timebuf, timebuf_size, "%lu year%s",
				years, (years > 1 ? "s" : ""));
		if (months)
			snprintf(timebuf + n, timebuf_size - n,
					", %lu month%s ago",
					months, (months > 1 ? "s" : ""));
		else
			snprintf(timebuf + n, timebuf_size - n, " ago");
		return timebuf;
	}
	/* Otherwise, just years. Centuries is probably overkill. */
	snprintf(timebuf, timebuf_size, "%lu years ago", (diff + 183) / 365);
	return timebuf;
}

const char *show_date(unsigned long time, int tz, enum date_mode mode)
{
	struct tm *tm;
	static char timebuf[200];

	if (mode == DATE_RAW) {
		snprintf(timebuf, sizeof(timebuf), "%lu %+05d", time, tz);
		return timebuf;
	}

	if (mode == DATE_RELATIVE) {
		struct timeval now;
		gettimeofday(&now, NULL);
		return show_date_relative(time, tz, &now,
					  timebuf, sizeof(timebuf));
	}

	if (mode == DATE_LOCAL)
		tz = local_tzoffset(time);

	tm = time_to_tm(time, tz);
	if (!tm)
		return NULL;
	if (mode == DATE_SHORT)
		sprintf(timebuf, "%04d-%02d-%02d", tm->tm_year + 1900,
				tm->tm_mon + 1, tm->tm_mday);
	else if (mode == DATE_ISO8601)
		sprintf(timebuf, "%04d-%02d-%02d %02d:%02d:%02d %+05d",
				tm->tm_year + 1900,
				tm->tm_mon + 1,
				tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec,
				tz);
	else if (mode == DATE_RFC2822)
		sprintf(timebuf, "%.3s, %d %.3s %d %02d:%02d:%02d %+05d",
			weekday_names[tm->tm_wday], tm->tm_mday,
			month_names[tm->tm_mon], tm->tm_year + 1900,
			tm->tm_hour, tm->tm_min, tm->tm_sec, tz);
	else
		sprintf(timebuf, "%.3s %.3s %d %02d:%02d:%02d %d%c%+05d",
				weekday_names[tm->tm_wday],
				month_names[tm->tm_mon],
				tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec,
				tm->tm_year + 1900,
				(mode == DATE_LOCAL) ? 0 : ' ',
				tz);
	return timebuf;
}

/*
 * Check these. And note how it doesn't do the summer-time conversion.
 *
 * In my world, it's always summer, and things are probably a bit off
 * in other ways too.
 */
static const struct {
	const char *name;
	int offset;
	int dst;
} timezone_names[] = {
	{ "IDLW", -12, 0, },	/* International Date Line West */
	{ "NT",   -11, 0, },	/* Nome */
	{ "CAT",  -10, 0, },	/* Central Alaska */
	{ "HST",  -10, 0, },	/* Hawaii Standard */
	{ "HDT",  -10, 1, },	/* Hawaii Daylight */
	{ "YST",   -9, 0, },	/* Yukon Standard */
	{ "YDT",   -9, 1, },	/* Yukon Daylight */
	{ "PST",   -8, 0, },	/* Pacific Standard */
	{ "PDT",   -8, 1, },	/* Pacific Daylight */
	{ "MST",   -7, 0, },	/* Mountain Standard */
	{ "MDT",   -7, 1, },	/* Mountain Daylight */
	{ "CST",   -6, 0, },	/* Central Standard */
	{ "CDT",   -6, 1, },	/* Central Daylight */
	{ "EST",   -5, 0, },	/* Eastern Standard */
	{ "EDT",   -5, 1, },	/* Eastern Daylight */
	{ "AST",   -3, 0, },	/* Atlantic Standard */
	{ "ADT",   -3, 1, },	/* Atlantic Daylight */
	{ "WAT",   -1, 0, },	/* West Africa */

	{ "GMT",    0, 0, },	/* Greenwich Mean */
	{ "UTC",    0, 0, },	/* Universal (Coordinated) */
	{ "Z",      0, 0, },    /* Zulu, alias for UTC */

	{ "WET",    0, 0, },	/* Western European */
	{ "BST",    0, 1, },	/* British Summer */
	{ "CET",   +1, 0, },	/* Central European */
	{ "MET",   +1, 0, },	/* Middle European */
	{ "MEWT",  +1, 0, },	/* Middle European Winter */
	{ "MEST",  +1, 1, },	/* Middle European Summer */
	{ "CEST",  +1, 1, },	/* Central European Summer */
	{ "MESZ",  +1, 1, },	/* Middle European Summer */
	{ "FWT",   +1, 0, },	/* French Winter */
	{ "FST",   +1, 1, },	/* French Summer */
	{ "EET",   +2, 0, },	/* Eastern Europe, USSR Zone 1 */
	{ "EEST",  +2, 1, },	/* Eastern European Daylight */
	{ "WAST",  +7, 0, },	/* West Australian Standard */
	{ "WADT",  +7, 1, },	/* West Australian Daylight */
	{ "CCT",   +8, 0, },	/* China Coast, USSR Zone 7 */
	{ "JST",   +9, 0, },	/* Japan Standard, USSR Zone 8 */
	{ "EAST", +10, 0, },	/* Eastern Australian Standard */
	{ "EADT", +10, 1, },	/* Eastern Australian Daylight */
	{ "GST",  +10, 0, },	/* Guam Standard, USSR Zone 9 */
	{ "NZT",  +12, 0, },	/* New Zealand */
	{ "NZST", +12, 0, },	/* New Zealand Standard */
	{ "NZDT", +12, 1, },	/* New Zealand Daylight */
	{ "IDLE", +12, 0, },	/* International Date Line East */
};

static int match_string(const char *date, const char *str)
{
	int i = 0;

	for (i = 0; *date; date++, str++, i++) {
		if (*date == *str)
			continue;
		if (toupper(*date) == toupper(*str))
			continue;
		if (!isalnum(*date))
			break;
		return 0;
	}
	return i;
}

static int skip_alpha(const char *date)
{
	int i = 0;
	do {
		i++;
	} while (isalpha(date[i]));
	return i;
}

/*
* Parse month, weekday, or timezone name
*/
static int match_alpha(const char *date, struct tm *tm, int *offset)
{
	int i;

	for (i = 0; i < 12; i++) {
		int match = match_string(date, month_names[i]);
		if (match >= 3) {
			tm->tm_mon = i;
			return match;
		}
	}

	for (i = 0; i < 7; i++) {
		int match = match_string(date, weekday_names[i]);
		if (match >= 3) {
			tm->tm_wday = i;
			return match;
		}
	}

	for (i = 0; i < ARRAY_SIZE(timezone_names); i++) {
		int match = match_string(date, timezone_names[i].name);
		if (match >= 3 || match == strlen(timezone_names[i].name)) {
			int off = timezone_names[i].offset;

			/* This is bogus, but we like summer */
			off += timezone_names[i].dst;

			/* Only use the tz name offset if we don't have anything better */
			if (*offset == -1)
				*offset = 60*off;

			return match;
		}
	}

	if (match_string(date, "PM") == 2) {
		tm->tm_hour = (tm->tm_hour % 12) + 12;
		return 2;
	}

	if (match_string(date, "AM") == 2) {
		tm->tm_hour = (tm->tm_hour % 12) + 0;
		return 2;
	}

	/* BAD CRAP */
	return skip_alpha(date);
}

static int is_date(int year, int month, int day, struct tm *now_tm, time_t now, struct tm *tm)
{
	if (month > 0 && month < 13 && day > 0 && day < 32) {
		struct tm check = *tm;
		struct tm *r = (now_tm ? &check : tm);
		time_t specified;

		r->tm_mon = month - 1;
		r->tm_mday = day;
		if (year == -1) {
			if (!now_tm)
				return 1;
			r->tm_year = now_tm->tm_year;
		}
		else if (year >= 1970 && year < 2100)
			r->tm_year = year - 1900;
		else if (year > 70 && year < 100)
			r->tm_year = year;
		else if (year < 38)
			r->tm_year = year + 100;
		else
			return 0;
		if (!now_tm)
			return 1;

		specified = tm_to_time_t(r);

		/* Be it commit time or author time, it does not make
		 * sense to specify timestamp way into the future.  Make
		 * sure it is not later than ten days from now...
		 */
		if (now + 10*24*3600 < specified)
			return 0;
		tm->tm_mon = r->tm_mon;
		tm->tm_mday = r->tm_mday;
		if (year != -1)
			tm->tm_year = r->tm_year;
		return 1;
	}
	return 0;
}

static int match_multi_number(unsigned long num, char c, const char *date, char *end, struct tm *tm)
{
	time_t now;
	struct tm now_tm;
	struct tm *refuse_future;
	long num2, num3;

	num2 = strtol(end+1, &end, 10);
	num3 = -1;
	if (*end == c && isdigit(end[1]))
		num3 = strtol(end+1, &end, 10);

	/* Time? Date? */
	switch (c) {
	case ':':
		if (num3 < 0)
			num3 = 0;
		if (num < 25 && num2 >= 0 && num2 < 60 && num3 >= 0 && num3 <= 60) {
			tm->tm_hour = num;
			tm->tm_min = num2;
			tm->tm_sec = num3;
			break;
		}
		return 0;

	case '-':
	case '/':
	case '.':
		now = time(NULL);
		refuse_future = NULL;
		if (gmtime_r(&now, &now_tm))
			refuse_future = &now_tm;

		if (num > 70) {
			/* yyyy-mm-dd? */
			if (is_date(num, num2, num3, refuse_future, now, tm))
				break;
			/* yyyy-dd-mm? */
			if (is_date(num, num3, num2, refuse_future, now, tm))
				break;
		}
		/* Our eastern European friends say dd.mm.yy[yy]
		 * is the norm there, so giving precedence to
		 * mm/dd/yy[yy] form only when separator is not '.'
		 */
		if (c != '.' &&
		    is_date(num3, num, num2, refuse_future, now, tm))
			break;
		/* European dd.mm.yy[yy] or funny US dd/mm/yy[yy] */
		if (is_date(num3, num2, num, refuse_future, now, tm))
			break;
		/* Funny European mm.dd.yy */
		if (c == '.' &&
		    is_date(num3, num, num2, refuse_future, now, tm))
			break;
		return 0;
	}
	return end - date;
}

/*
 * Have we filled in any part of the time/date yet?
 * We just do a binary 'and' to see if the sign bit
 * is set in all the values.
 */
static inline int nodate(struct tm *tm)
{
	return (tm->tm_year &
		tm->tm_mon &
		tm->tm_mday &
		tm->tm_hour &
		tm->tm_min &
		tm->tm_sec) < 0;
}

/*
 * We've seen a digit. Time? Year? Date?
 */
static int match_digit(const char *date, struct tm *tm, int *offset, int *tm_gmt)
{
	int n;
	char *end;
	unsigned long num;

	num = strtoul(date, &end, 10);

	/*
	 * Seconds since 1970? We trigger on that for any numbers with
	 * more than 8 digits. This is because we don't want to rule out
	 * numbers like 20070606 as a YYYYMMDD date.
	 */
	if (num >= 100000000 && nodate(tm)) {
		time_t time = num;
		if (gmtime_r(&time, tm)) {
			*tm_gmt = 1;
			return end - date;
		}
	}

	/*
	 * Check for special formats: num[-.:/]num[same]num
	 */
	switch (*end) {
	case ':':
	case '.':
	case '/':
	case '-':
		if (isdigit(end[1])) {
			int match = match_multi_number(num, *end, date, end, tm);
			if (match)
				return match;
		}
	}

	/*
	 * None of the special formats? Try to guess what
	 * the number meant. We use the number of digits
	 * to make a more educated guess..
	 */
	n = 0;
	do {
		n++;
	} while (isdigit(date[n]));

	/* Four-digit year or a timezone? */
	if (n == 4) {
		if (num <= 1400 && *offset == -1) {
			unsigned int minutes = num % 100;
			unsigned int hours = num / 100;
			*offset = hours*60 + minutes;
		} else if (num > 1900 && num < 2100)
			tm->tm_year = num - 1900;
		return n;
	}

	/*
	 * Ignore lots of numerals. We took care of 4-digit years above.
	 * Days or months must be one or two digits.
	 */
	if (n > 2)
		return n;

	/*
	 * NOTE! We will give precedence to day-of-month over month or
	 * year numbers in the 1-12 range. So 05 is always "mday 5",
	 * unless we already have a mday..
	 *
	 * IOW, 01 Apr 05 parses as "April 1st, 2005".
	 */
	if (num > 0 && num < 32 && tm->tm_mday < 0) {
		tm->tm_mday = num;
		return n;
	}

	/* Two-digit year? */
	if (n == 2 && tm->tm_year < 0) {
		if (num < 10 && tm->tm_mday >= 0) {
			tm->tm_year = num + 100;
			return n;
		}
		if (num >= 70) {
			tm->tm_year = num;
			return n;
		}
	}

	if (num > 0 && num < 13 && tm->tm_mon < 0)
		tm->tm_mon = num-1;

	return n;
}

static int match_tz(const char *date, int *offp)
{
	char *end;
	int offset = strtoul(date+1, &end, 10);
	int min, hour;
	int n = end - date - 1;

	min = offset % 100;
	hour = offset / 100;

	/*
	 * Don't accept any random crap.. At least 3 digits, and
	 * a valid minute. We might want to check that the minutes
	 * are divisible by 30 or something too.
	 */
	if (min < 60 && n > 2) {
		offset = hour*60+min;
		if (*date == '-')
			offset = -offset;

		*offp = offset;
	}
	return end - date;
}

static int date_string(unsigned long date, int offset, char *buf, int len)
{
	int sign = '+';

	if (offset < 0) {
		offset = -offset;
		sign = '-';
	}
	return snprintf(buf, len, "%lu %c%02d%02d", date, sign, offset / 60, offset % 60);
}

/* Gr. strptime is crap for this; it doesn't have a way to require RFC2822
   (i.e. English) day/month names, and it doesn't work correctly with %z. */
int parse_date_toffset(const char *date, unsigned long *timestamp, int *offset)
{
	struct tm tm;
	int tm_gmt;
	unsigned long dummy_timestamp;
	int dummy_offset;

	if (!timestamp)
		timestamp = &dummy_timestamp;
	if (!offset)
		offset = &dummy_offset;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = -1;
	tm.tm_mon = -1;
	tm.tm_mday = -1;
	tm.tm_isdst = -1;
	tm.tm_hour = -1;
	tm.tm_min = -1;
	tm.tm_sec = -1;
	*offset = -1;
	tm_gmt = 0;

	for (;;) {
		int match = 0;
		unsigned char c = *date;

		/* Stop at end of string or newline */
		if (!c || c == '\n')
			break;

		if (isalpha(c))
			match = match_alpha(date, &tm, offset);
		else if (isdigit(c))
			match = match_digit(date, &tm, offset, &tm_gmt);
		else if ((c == '-' || c == '+') && isdigit(date[1]))
			match = match_tz(date, offset);

		if (!match) {
			/* BAD CRAP */
			match = 1;
		}

		date += match;
	}

	/* mktime uses local timezone */
	*timestamp = tm_to_time_t(&tm);
	if (*offset == -1)
		*offset = ((time_t)*timestamp - mktime(&tm)) / 60;

	if (*timestamp == -1)
		return -1;

	if (!tm_gmt)
		*timestamp -= *offset * 60;
	return 1; /* success */
}

int parse_date(const char *date, char *result, int maxlen)
{
	unsigned long timestamp;
	int offset;
	if (parse_date_toffset(date, &timestamp, &offset) > 0)
		return date_string(timestamp, offset, result, maxlen);
	else
		return -1;
}

enum date_mode parse_date_format(const char *format)
{
	if (!strcmp(format, "relative"))
		return DATE_RELATIVE;
	else if (!strcmp(format, "iso8601") ||
		 !strcmp(format, "iso"))
		return DATE_ISO8601;
	else if (!strcmp(format, "rfc2822") ||
		 !strcmp(format, "rfc"))
		return DATE_RFC2822;
	else if (!strcmp(format, "short"))
		return DATE_SHORT;
	else if (!strcmp(format, "local"))
		return DATE_LOCAL;
	else if (!strcmp(format, "default"))
		return DATE_NORMAL;
	else if (!strcmp(format, "raw"))
		return DATE_RAW;
	else
		die("unknown date format %s", format);
}

void datestamp(char *buf, int bufsize)
{
	time_t now;
	int offset;

	time(&now);

	offset = tm_to_time_t(localtime(&now)) - now;
	offset /= 60;

	date_string(now, offset, buf, bufsize);
}

/*
 * Relative time update (eg "2 days ago").  If we haven't set the time
 * yet, we need to set it from current time.
 */
static unsigned long update_tm(struct tm *tm, struct tm *now, unsigned long sec)
{
	time_t n;

	if (tm->tm_mday < 0)
		tm->tm_mday = now->tm_mday;
	if (tm->tm_mon < 0)
		tm->tm_mon = now->tm_mon;
	if (tm->tm_year < 0) {
		tm->tm_year = now->tm_year;
		if (tm->tm_mon > now->tm_mon)
			tm->tm_year--;
	}

	n = mktime(tm) - sec;
	localtime_r(&n, tm);
	return n;
}

static void date_now(struct tm *tm, struct tm *now, int *num)
{
	update_tm(tm, now, 0);
}

static void date_yesterday(struct tm *tm, struct tm *now, int *num)
{
	update_tm(tm, now, 24*60*60);
}

static void date_time(struct tm *tm, struct tm *now, int hour)
{
	if (tm->tm_hour < hour)
		date_yesterday(tm, now, NULL);
	tm->tm_hour = hour;
	tm->tm_min = 0;
	tm->tm_sec = 0;
}

static void date_midnight(struct tm *tm, struct tm *now, int *num)
{
	date_time(tm, now, 0);
}

static void date_noon(struct tm *tm, struct tm *now, int *num)
{
	date_time(tm, now, 12);
}

static void date_tea(struct tm *tm, struct tm *now, int *num)
{
	date_time(tm, now, 17);
}

static void date_pm(struct tm *tm, struct tm *now, int *num)
{
	int hour, n = *num;
	*num = 0;

	hour = tm->tm_hour;
	if (n) {
		hour = n;
		tm->tm_min = 0;
		tm->tm_sec = 0;
	}
	tm->tm_hour = (hour % 12) + 12;
}

static void date_am(struct tm *tm, struct tm *now, int *num)
{
	int hour, n = *num;
	*num = 0;

	hour = tm->tm_hour;
	if (n) {
		hour = n;
		tm->tm_min = 0;
		tm->tm_sec = 0;
	}
	tm->tm_hour = (hour % 12);
}

static void date_never(struct tm *tm, struct tm *now, int *num)
{
	time_t n = 0;
	localtime_r(&n, tm);
}

static const struct special {
	const char *name;
	void (*fn)(struct tm *, struct tm *, int *);
} special[] = {
	{ "yesterday", date_yesterday },
	{ "noon", date_noon },
	{ "midnight", date_midnight },
	{ "tea", date_tea },
	{ "PM", date_pm },
	{ "AM", date_am },
	{ "never", date_never },
	{ "now", date_now },
	{ NULL }
};

static const char *number_name[] = {
	"zero", "one", "two", "three", "four",
	"five", "six", "seven", "eight", "nine", "ten",
};

static const struct typelen {
	const char *type;
	int length;
} typelen[] = {
	{ "seconds", 1 },
	{ "minutes", 60 },
	{ "hours", 60*60 },
	{ "days", 24*60*60 },
	{ "weeks", 7*24*60*60 },
	{ NULL }
};

static const char *approxidate_alpha(const char *date, struct tm *tm, struct tm *now, int *num, int *touched)
{
	const struct typelen *tl;
	const struct special *s;
	const char *end = date;
	int i;

	while (isalpha(*++end));
		;

	for (i = 0; i < 12; i++) {
		int match = match_string(date, month_names[i]);
		if (match >= 3) {
			tm->tm_mon = i;
			*touched = 1;
			return end;
		}
	}

	for (s = special; s->name; s++) {
		int len = strlen(s->name);
		if (match_string(date, s->name) == len) {
			s->fn(tm, now, num);
			*touched = 1;
			return end;
		}
	}

	if (!*num) {
		for (i = 1; i < 11; i++) {
			int len = strlen(number_name[i]);
			if (match_string(date, number_name[i]) == len) {
				*num = i;
				*touched = 1;
				return end;
			}
		}
		if (match_string(date, "last") == 4) {
			*num = 1;
			*touched = 1;
		}
		return end;
	}

	tl = typelen;
	while (tl->type) {
		int len = strlen(tl->type);
		if (match_string(date, tl->type) >= len-1) {
			update_tm(tm, now, tl->length * *num);
			*num = 0;
			*touched = 1;
			return end;
		}
		tl++;
	}

	for (i = 0; i < 7; i++) {
		int match = match_string(date, weekday_names[i]);
		if (match >= 3) {
			int diff, n = *num -1;
			*num = 0;

			diff = tm->tm_wday - i;
			if (diff <= 0)
				n++;
			diff += 7*n;

			update_tm(tm, now, diff * 24 * 60 * 60);
			*touched = 1;
			return end;
		}
	}

	if (match_string(date, "months") >= 5) {
		int n;
		update_tm(tm, now, 0); /* fill in date fields if needed */
		n = tm->tm_mon - *num;
		*num = 0;
		while (n < 0) {
			n += 12;
			tm->tm_year--;
		}
		tm->tm_mon = n;
		*touched = 1;
		return end;
	}

	if (match_string(date, "years") >= 4) {
		update_tm(tm, now, 0); /* fill in date fields if needed */
		tm->tm_year -= *num;
		*num = 0;
		*touched = 1;
		return end;
	}

	return end;
}

static const char *approxidate_digit(const char *date, struct tm *tm, int *num)
{
	char *end;
	unsigned long number = strtoul(date, &end, 10);

	switch (*end) {
	case ':':
	case '.':
	case '/':
	case '-':
		if (isdigit(end[1])) {
			int match = match_multi_number(number, *end, date, end, tm);
			if (match)
				return date + match;
		}
	}

	/* Accept zero-padding only for small numbers ("Dec 02", never "Dec 0002") */
	if (date[0] != '0' || end - date <= 2)
		*num = number;
	return end;
}

/*
 * Do we have a pending number at the end, or when
 * we see a new one? Let's assume it's a month day,
 * as in "Dec 6, 1992"
 */
static void pending_number(struct tm *tm, int *num)
{
	int number = *num;

	if (number) {
		*num = 0;
		if (tm->tm_mday < 0 && number < 32)
			tm->tm_mday = number;
		else if (tm->tm_mon < 0 && number < 13)
			tm->tm_mon = number-1;
		else if (tm->tm_year < 0) {
			if (number > 1969 && number < 2100)
				tm->tm_year = number - 1900;
			else if (number > 69 && number < 100)
				tm->tm_year = number;
			else if (number < 38)
				tm->tm_year = 100 + number;
			/* We screw up for number = 00 ? */
		}
	}
}

static unsigned long approxidate_str(const char *date,
				     const struct timeval *tv,
				     int *error_ret)
{
	int number = 0;
	int touched = 0;
	struct tm tm, now;
	time_t time_sec;

	time_sec = tv->tv_sec;
	localtime_r(&time_sec, &tm);
	now = tm;

	tm.tm_year = -1;
	tm.tm_mon = -1;
	tm.tm_mday = -1;

	for (;;) {
		unsigned char c = *date;
		if (!c)
			break;
		date++;
		if (isdigit(c)) {
			pending_number(&tm, &number);
			date = approxidate_digit(date-1, &tm, &number);
			touched = 1;
			continue;
		}
		if (isalpha(c))
			date = approxidate_alpha(date-1, &tm, &now, &number, &touched);
	}
	pending_number(&tm, &number);
	if (!touched)
		*error_ret = 1;
	return update_tm(&tm, &now, 0);
}

unsigned long approxidate_relative(const char *date, const struct timeval *tv)
{
	unsigned long timestamp;
	int offset;
	int errors = 0;

	if (parse_date_toffset(date, &timestamp, &offset) > 0)
		return timestamp;

	return approxidate_str(date, tv, &errors);
}

unsigned long approxidate_careful(const char *date, int *error_ret)
{
	struct timeval tv;
	unsigned long timestamp;
	int offset;
	int dummy = 0;
	if (!error_ret)
		error_ret = &dummy;

	if (parse_date_toffset(date, &timestamp, &offset) > 0) {
		*error_ret = 0;
		return timestamp;
	}

	gettimeofday(&tv, NULL);
	return approxidate_str(date, &tv, error_ret);
}
