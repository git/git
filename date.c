/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */

#include <ctype.h>
#include <time.h>

#include "cache.h"

static time_t my_mktime(struct tm *tm)
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
	return (year * 365 + (year + 1) / 4 + mdays[month] + day) * 24*60*60UL +
		tm->tm_hour * 60*60 + tm->tm_min * 60 + tm->tm_sec;
}

static const char *month_names[] = {
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
};

static const char *weekday_names[] = {
	"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

/*
 * The "tz" thing is passed in as this strange "decimal parse of tz"
 * thing, which means that tz -0100 is passed in as the integer -100,
 * even though it means "sixty minutes off"
 */
const char *show_date(unsigned long time, int tz)
{
	struct tm *tm;
	time_t t;
	static char timebuf[200];
	int minutes;

	minutes = tz < 0 ? -tz : tz;
	minutes = (minutes / 100)*60 + (minutes % 100);
	minutes = tz < 0 ? -minutes : minutes;
	t = time + minutes * 60;
	tm = gmtime(&t);
	if (!tm)
		return NULL;
	sprintf(timebuf, "%.3s %.3s %d %02d:%02d:%02d %d %+05d",
		weekday_names[tm->tm_wday],
		month_names[tm->tm_mon],
		tm->tm_mday,
		tm->tm_hour, tm->tm_min, tm->tm_sec,
		tm->tm_year + 1900, tz);
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
	{ "NZT",  +11, 0, },	/* New Zealand */
	{ "NZST", +11, 0, },	/* New Zealand Standard */
	{ "NZDT", +11, 1, },	/* New Zealand Daylight */
	{ "IDLE", +12, 0, },	/* International Date Line East */
};

#define NR_TZ (sizeof(timezone_names) / sizeof(timezone_names[0]))
	
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

	for (i = 0; i < NR_TZ; i++) {
		int match = match_string(date, timezone_names[i].name);
		if (match >= 3) {
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
		if (tm->tm_hour > 0 && tm->tm_hour < 12)
			tm->tm_hour += 12;
		return 2;
	}

	/* BAD CRAP */
	return skip_alpha(date);
}

static int is_date(int year, int month, int day, struct tm *tm)
{
	if (month > 0 && month < 13 && day > 0 && day < 32) {
		if (year == -1) {
			tm->tm_mon = month-1;
			tm->tm_mday = day;
			return 1;
		}
		if (year >= 1970 && year < 2100) {
			year -= 1900;
		} else if (year > 70 && year < 100) {
			/* ok */
		} else if (year < 38) {
			year += 100;
		} else
			return 0;

		tm->tm_mon = month-1;
		tm->tm_mday = day;
		tm->tm_year = year;
		return 1;
	}
	return 0;
}

static int match_multi_number(unsigned long num, char c, char *date, char *end, struct tm *tm)
{
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
		if (num > 70) {
			/* yyyy-mm-dd? */
			if (is_date(num, num2, num3, tm))
				break;
			/* yyyy-dd-mm? */
			if (is_date(num, num3, num2, tm))
				break;
		}
		/* mm/dd/yy ? */
		if (is_date(num3, num2, num, tm))
			break;
		/* dd/mm/yy ? */
		if (is_date(num3, num, num2, tm))
			break;
		return 0;
	}
	return end - date;
}

/*
 * We've seen a digit. Time? Year? Date? 
 */
static int match_digit(char *date, struct tm *tm, int *offset, int *tm_gmt)
{
	int n;
	char *end;
	unsigned long num;

	num = strtoul(date, &end, 10);

	/*
	 * Seconds since 1970? We trigger on that for anything after Jan 1, 2000
	 */
	if (num > 946684800) {
		time_t time = num;
		if (gmtime_r(&time, tm)) {
			*tm_gmt = 1;
			return end - date;
		}
	}

	/*
	 * Check for special formats: num[:-/]num[same]num
	 */
	switch (*end) {
	case ':':
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
		if (num <= 1200 && *offset == -1) {
			unsigned int minutes = num % 100;
			unsigned int hours = num / 100;
			*offset = hours*60 + minutes;
		} else if (num > 1900 && num < 2100)
			tm->tm_year = num - 1900;
		return n;
	}

	/*
	 * NOTE! We will give precedence to day-of-month over month or
	 * year numebers in the 1-12 range. So 05 is always "mday 5",
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

	if (num > 0 && num < 32) {
		tm->tm_mday = num;
	} else if (num > 1900) {
		tm->tm_year = num - 1900;
	} else if (num > 70) {
		tm->tm_year = num;
	} else if (num > 0 && num < 13) {
		tm->tm_mon = num-1;
	}
		
	return n;
}

static int match_tz(char *date, int *offp)
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

/* Gr. strptime is crap for this; it doesn't have a way to require RFC2822
   (i.e. English) day/month names, and it doesn't work correctly with %z. */
void parse_date(char *date, char *result, int maxlen)
{
	struct tm tm;
	int offset, sign, tm_gmt;
	time_t then;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = -1;
	tm.tm_mon = -1;
	tm.tm_mday = -1;
	tm.tm_isdst = -1;
	offset = -1;
	tm_gmt = 0;

	for (;;) {
		int match = 0;
		unsigned char c = *date;

		/* Stop at end of string or newline */
		if (!c || c == '\n')
			break;

		if (isalpha(c))
			match = match_alpha(date, &tm, &offset);
		else if (isdigit(c))
			match = match_digit(date, &tm, &offset, &tm_gmt);
		else if ((c == '-' || c == '+') && isdigit(date[1]))
			match = match_tz(date, &offset);

		if (!match) {
			/* BAD CRAP */
			match = 1;
		}	

		date += match;
	}

	/* mktime uses local timezone */
	then = my_mktime(&tm); 
	if (offset == -1)
		offset = (then - mktime(&tm)) / 60;

	if (then == -1)
		return;

	if (!tm_gmt)
		then -= offset * 60;

	sign = '+';
	if (offset < 0) {
		offset = -offset;
		sign = '-';
	}

	snprintf(result, maxlen, "%lu %c%02d%02d", then, sign, offset/60, offset % 60);
}

void datestamp(char *buf, int bufsize)
{
	time_t now;
	int offset;

	time(&now);

	offset = my_mktime(localtime(&now)) - now;
	offset /= 60;

	snprintf(buf, bufsize, "%lu %+05d", now, offset/60*100 + offset%60);
}
