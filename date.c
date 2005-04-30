/*
 * GIT - The information manager from hell
 *
 * Copyright (C) Linus Torvalds, 2005
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

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

	/* BAD CRAP */
	return 0;
}

static int match_digit(char *date, struct tm *tm, int *offset)
{
	char *end, c;
	unsigned long num, num2, num3;

	num = strtoul(date, &end, 10);

	/* Time? num:num[:num] */
	if (num < 24 && end[0] == ':' && isdigit(end[1])) {
		tm->tm_hour = num;
		num = strtoul(end+1, &end, 10);
		if (num < 60) {
			tm->tm_min = num;
			if (end[0] == ':' && isdigit(end[1])) {
				num = strtoul(end+1, &end, 10);
				if (num < 61)
					tm->tm_sec = num;
			}
		}
		return end - date;
	}

	/* Year? Day of month? Numeric date-string?*/
	c = *end;
	switch (c) {
	default:
		if (num > 0 && num < 32) {
			tm->tm_mday = num;
			break;
		}
		if (num > 1900) {
			tm->tm_year = num - 1900;
			break;
		}
		if (num > 70) {
			tm->tm_year = num;
			break;
		}
		break;

	case '-':
	case '/':
		if (num && num < 32 && isdigit(end[1])) {
			num2 = strtoul(end+1, &end, 10);
			if (!num2 || num2 > 31)
				break;
			if (num > 12) {
				if (num2 > 12)
					break;
				num3 = num;
				num  = num2;
				num2 = num3;
			}
			tm->tm_mon = num - 1;
			tm->tm_mday = num2;
			if (*end == c && isdigit(end[1])) {
				num3 = strtoul(end+1, &end, 10);
				if (num3 > 1900)
					num3 -= 1900;
				else if (num3 < 38)
					num3 += 100;
				tm->tm_year = num3;
			}
			break;
		}
	}
		
	return end - date;
			
}

static int match_tz(char *date, int *offp)
{
	char *end;
	int offset = strtoul(date+1, &end, 10);
	int min, hour;

	min = offset % 100;
	hour = offset / 100;

	offset = hour*60+min;
	if (*date == '-')
		offset = -offset;

	*offp = offset;
	return end - date;
}

/* Gr. strptime is crap for this; it doesn't have a way to require RFC2822
   (i.e. English) day/month names, and it doesn't work correctly with %z. */
void parse_date(char *date, char *result, int maxlen)
{
	struct tm tm;
	int offset, sign;
	time_t then;

	memset(&tm, 0, sizeof(tm));
	tm.tm_year = -1;
	tm.tm_mon = -1;
	tm.tm_mday = -1;
	tm.tm_isdst = -1;
	offset = -1;

	for (;;) {
		int match = 0;
		unsigned char c = *date;

		/* Stop at end of string or newline */
		if (!c || c == '\n')
			break;

		if (isalpha(c))
			match = match_alpha(date, &tm, &offset);
		else if (isdigit(c))
			match = match_digit(date, &tm, &offset);
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
