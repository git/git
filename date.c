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
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char *weekday_names[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};


static char *skipfws(char *str)
{
	while (isspace(*str))
		str++;
	return str;
}

	
/* Gr. strptime is crap for this; it doesn't have a way to require RFC2822
   (i.e. English) day/month names, and it doesn't work correctly with %z. */
void parse_date(char *date, char *result, int maxlen)
{
	struct tm tm;
	char *p, *tz;
	int i, offset;
	time_t then;

	memset(&tm, 0, sizeof(tm));

	/* Skip day-name */
	p = skipfws(date);
	if (!isdigit(*p)) {
		for (i=0; i<7; i++) {
			if (!strncmp(p,weekday_names[i],3) && p[3] == ',') {
				p = skipfws(p+4);
				goto day;
			}
		}
		return;
	}					

	/* day */
 day:
	tm.tm_mday = strtoul(p, &p, 10);

	if (tm.tm_mday < 1 || tm.tm_mday > 31)
		return;

	if (!isspace(*p))
		return;

	p = skipfws(p);

	/* month */

	for (i=0; i<12; i++) {
		if (!strncmp(p, month_names[i], 3) && isspace(p[3])) {
			tm.tm_mon = i;
			p = skipfws(p+strlen(month_names[i]));
			goto year;
		}
	}
	return; /* Error -- bad month */

	/* year */
 year:	
	tm.tm_year = strtoul(p, &p, 10);

	if (!tm.tm_year && !isspace(*p))
		return;

	if (tm.tm_year > 1900)
		tm.tm_year -= 1900;
		
	p=skipfws(p);

	/* hour */
	if (!isdigit(*p))
		return;
	tm.tm_hour = strtoul(p, &p, 10);
	
	if (tm.tm_hour > 23)
		return;

	if (*p != ':')
		return; /* Error -- bad time */
	p++;

	/* minute */
	if (!isdigit(*p))
		return;
	tm.tm_min = strtoul(p, &p, 10);
	
	if (tm.tm_min > 59)
		return;

	if (*p != ':')
		goto zone;
	p++;

	/* second */
	if (!isdigit(*p))
		return;
	tm.tm_sec = strtoul(p, &p, 10);
	
	if (tm.tm_sec > 60)
		return;

 zone:
	if (!isspace(*p))
		return;

	p = skipfws(p);

	if (*p == '-')
		offset = -60;
	else if (*p == '+')
		offset = 60;
	else
	       return;

	if (!isdigit(p[1]) || !isdigit(p[2]) || !isdigit(p[3]) || !isdigit(p[4]))
		return;

	tz = p;
	i = strtoul(p+1, NULL, 10);
	offset *= ((i % 100) + ((i / 100) * 60));

	p = skipfws(p + 5);
	if (*p && *p != '(') /* trailing comment like (EDT) is ok */
		return;

	then = my_mktime(&tm); /* mktime uses local timezone */
	if (then == -1)
		return;

	then -= offset;

	snprintf(result, maxlen, "%lu %5.5s", then, tz);
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
