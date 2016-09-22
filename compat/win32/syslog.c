#include "../../git-compat-util.h"

static HANDLE ms_eventlog;

void openlog(const char *ident, int logopt, int facility)
{
	if (ms_eventlog)
		return;

	ms_eventlog = RegisterEventSourceA(NULL, ident);

	if (!ms_eventlog)
		warning("RegisterEventSource() failed: %lu", GetLastError());
}

void syslog(int priority, const char *fmt, ...)
{
	WORD logtype;
	char *str, *pos;
	int str_len;
	va_list ap;

	if (!ms_eventlog)
		return;

	va_start(ap, fmt);
	str_len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	if (str_len < 0) {
		warning_errno("vsnprintf failed");
		return;
	}

	str = malloc(st_add(str_len, 1));
	if (!str) {
		warning_errno("malloc failed");
		return;
	}

	va_start(ap, fmt);
	vsnprintf(str, str_len + 1, fmt, ap);
	va_end(ap);

	while ((pos = strstr(str, "%1")) != NULL) {
		str = realloc(str, st_add(++str_len, 1));
		if (!str) {
			warning_errno("realloc failed");
			return;
		}
		memmove(pos + 2, pos + 1, strlen(pos));
		pos[1] = ' ';
	}

	switch (priority) {
	case LOG_EMERG:
	case LOG_ALERT:
	case LOG_CRIT:
	case LOG_ERR:
		logtype = EVENTLOG_ERROR_TYPE;
		break;

	case LOG_WARNING:
		logtype = EVENTLOG_WARNING_TYPE;
		break;

	case LOG_NOTICE:
	case LOG_INFO:
	case LOG_DEBUG:
	default:
		logtype = EVENTLOG_INFORMATION_TYPE;
		break;
	}

	ReportEventA(ms_eventlog, logtype, 0, 0, NULL, 1, 0,
	    (const char **)&str, NULL);
	free(str);
}
