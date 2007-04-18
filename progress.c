#include "git-compat-util.h"
#include "progress.h"

static volatile sig_atomic_t progress_update;

static void progress_interval(int signum)
{
	progress_update = 1;
}

static void set_progress_signal(void)
{
	struct sigaction sa;
	struct itimerval v;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = progress_interval;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, NULL);

	v.it_interval.tv_sec = 1;
	v.it_interval.tv_usec = 0;
	v.it_value = v.it_interval;
	setitimer(ITIMER_REAL, &v, NULL);
}

static void clear_progress_signal(void)
{
	struct itimerval v = {{0,},};
	setitimer(ITIMER_REAL, &v, NULL);
	signal(SIGALRM, SIG_IGN);
	progress_update = 0;
}

int display_progress(struct progress *progress, unsigned n)
{
	if (progress->total) {
		unsigned percent = n * 100 / progress->total;
		if (percent != progress->last_percent || progress_update) {
			progress->last_percent = percent;
			fprintf(stderr, "%s%4u%% (%u/%u) done\r",
				progress->msg, percent, n, progress->total);
			progress_update = 0;
			return 1;
		}
	} else if (progress_update) {
		fprintf(stderr, "%s%u\r", progress->msg, n);
		progress_update = 0;
		return 1;
	}
	return 0;
}

void start_progress(struct progress *progress, const char *msg, unsigned total)
{
	progress->msg = msg;
	progress->total = total;
	progress->last_percent = -1;
	set_progress_signal();
}

void stop_progress(struct progress *progress)
{
	clear_progress_signal();
	if (progress->total)
		fputc('\n', stderr);
}
