#include "git-compat-util.h"
#include "progress.h"

static volatile sig_atomic_t progress_update;

#ifndef __MINGW32__
static void progress_interval(int signum)
{
	progress_update = 1;
}
#else
static HANDLE progress_event;
static HANDLE progress_thread;

static __stdcall unsigned heartbeat(void *dummy)
{
	while (WaitForSingleObject(progress_event, 1000) == WAIT_TIMEOUT)
		progress_update = 1;
	return 0;
}
#endif

static void set_progress_signal(void)
{
#ifndef __MINGW32__
	struct sigaction sa;
	struct itimerval v;

	progress_update = 0;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = progress_interval;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, NULL);

	v.it_interval.tv_sec = 1;
	v.it_interval.tv_usec = 0;
	v.it_value = v.it_interval;
	setitimer(ITIMER_REAL, &v, NULL);
#else
	/* this is just eye candy: errors are not fatal */
	progress_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (progress_event) {
		progress_thread = (HANDLE) _beginthreadex(NULL, 0, heartbeat, NULL, 0, NULL);
		if (!progress_thread )
			error("cannot create progress indicator");
	} else
		error("cannot allocate resources for progress indicator");
#endif
}

static void clear_progress_signal(void)
{
#ifndef __MINGW32__
	struct itimerval v = {{0,},};
	setitimer(ITIMER_REAL, &v, NULL);
	signal(SIGALRM, SIG_IGN);
#else
	/* this is just eye candy: errors are not fatal */
	if (progress_event)
		SetEvent(progress_event);	/* tells thread to terminate */
	if (progress_thread) {
		int rc = WaitForSingleObject(progress_thread, 1000);
		if (rc == WAIT_TIMEOUT)
			error("progress thread did not terminate timely");
		else if (rc != WAIT_OBJECT_0)
			error("waiting for progress thread failed: %lu",
			      GetLastError());
		CloseHandle(progress_thread);
	}
	if (progress_event)
		CloseHandle(progress_event);
	progress_event = NULL;
	progress_thread = NULL;
#endif

	progress_update = 0;
}

int display_progress(struct progress *progress, unsigned n)
{
	if (progress->delay) {
		char buf[80];
		if (!progress_update || --progress->delay)
			return 0;
		if (progress->total) {
			unsigned percent = n * 100 / progress->total;
			if (percent > progress->delayed_percent_treshold) {
				/* inhibit this progress report entirely */
				clear_progress_signal();
				progress->delay = -1;
				progress->total = 0;
				return 0;
			}
		}
		if (snprintf(buf, sizeof(buf),
			     progress->delayed_title, progress->total))
			fprintf(stderr, "%s\n", buf);
	}
	if (progress->total) {
		unsigned percent = n * 100 / progress->total;
		if (percent != progress->last_percent || progress_update) {
			progress->last_percent = percent;
			fprintf(stderr, "%s%4u%% (%u/%u) done\r",
				progress->prefix, percent, n, progress->total);
			progress_update = 0;
			progress->need_lf = 1;
			return 1;
		}
	} else if (progress_update) {
		fprintf(stderr, "%s%u\r", progress->prefix, n);
		progress_update = 0;
		progress->need_lf = 1;
		return 1;
	}
	return 0;
}

void start_progress(struct progress *progress, const char *title,
		    const char *prefix, unsigned total)
{
	char buf[80];
	progress->prefix = prefix;
	progress->total = total;
	progress->last_percent = -1;
	progress->delay = 0;
	progress->need_lf = 0;
	if (snprintf(buf, sizeof(buf), title, total))
		fprintf(stderr, "%s\n", buf);
	set_progress_signal();
}

void start_progress_delay(struct progress *progress, const char *title,
			  const char *prefix, unsigned total,
			  unsigned percent_treshold, unsigned delay)
{
	progress->prefix = prefix;
	progress->total = total;
	progress->last_percent = -1;
	progress->delayed_percent_treshold = percent_treshold;
	progress->delayed_title = title;
	progress->delay = delay;
	progress->need_lf = 0;
	set_progress_signal();
}

void stop_progress(struct progress *progress)
{
	clear_progress_signal();
	if (progress->need_lf)
		fputc('\n', stderr);
}
