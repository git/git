#include "../git-compat-util.h"

unsigned int _CRT_fmode = _O_BINARY;

unsigned int sleep (unsigned int seconds)
{
	Sleep(seconds*1000);
	return 0;
}

int mkstemp(char *template)
{
	char *filename = mktemp(template);
	if (filename == NULL)
		return -1;
	return open(filename, O_RDWR | O_CREAT, 0600);
}

int gettimeofday(struct timeval *tv, void *tz)
{
	return -1;
}

int poll(struct pollfd *ufds, unsigned int nfds, int timeout)
{
	return -1;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	/* gmtime() in MSVCRT.DLL is thread-safe, but not reentrant */
	memcpy(result, gmtime(timep), sizeof(struct tm));
	return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	/* localtime() in MSVCRT.DLL is thread-safe, but not reentrant */
	memcpy(result, localtime(timep), sizeof(struct tm));
	return result;
}

struct passwd *getpwuid(int uid)
{
	static struct passwd p;
	return &p;
}

int setitimer(int type, struct itimerval *in, struct itimerval *out)
{
	return -1;
}

int sigaction(int sig, struct sigaction *in, struct sigaction *out)
{
	return -1;
}
