/*
 * Simple text-based progress display module for GIT
 *
 * Copyright (c) 2007 by Nicolas Pitre <nico@fluxnic.net>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "git-compat-util.h"
#include "progress.h"

#define TP_IDX_MAX      8

struct throughput {
	off_t curr_total;
	off_t prev_total;
	struct timeval prev_tv;
	unsigned int avg_bytes;
	unsigned int avg_misecs;
	unsigned int last_bytes[TP_IDX_MAX];
	unsigned int last_misecs[TP_IDX_MAX];
	unsigned int idx;
	char display[32];
};

struct progress {
	const char *title;
	int last_value;
	unsigned total;
	unsigned last_percent;
	unsigned delay;
	unsigned delayed_percent_treshold;
	struct throughput *throughput;
};

static volatile sig_atomic_t progress_update;

static void progress_interval(int signum)
{
	progress_update = 1;
}

static void set_progress_signal(void)
{
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
}

static void clear_progress_signal(void)
{
	struct itimerval v = {{0,},};
	setitimer(ITIMER_REAL, &v, NULL);
	signal(SIGALRM, SIG_IGN);
	progress_update = 0;
}

static int display(struct progress *progress, unsigned n, const char *done)
{
	const char *eol, *tp;

	if (progress->delay) {
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
	}

	progress->last_value = n;
	tp = (progress->throughput) ? progress->throughput->display : "";
	eol = done ? done : "   \r";
	if (progress->total) {
		unsigned percent = n * 100 / progress->total;
		if (percent != progress->last_percent || progress_update) {
			progress->last_percent = percent;
			fprintf(stderr, "%s: %3u%% (%u/%u)%s%s",
				progress->title, percent, n,
				progress->total, tp, eol);
			fflush(stderr);
			progress_update = 0;
			return 1;
		}
	} else if (progress_update) {
		fprintf(stderr, "%s: %u%s%s", progress->title, n, tp, eol);
		fflush(stderr);
		progress_update = 0;
		return 1;
	}

	return 0;
}

static void throughput_string(struct throughput *tp, off_t total,
			      unsigned int rate)
{
	int l = sizeof(tp->display);
	if (total > 1 << 30) {
		l -= snprintf(tp->display, l, ", %u.%2.2u GiB",
			      (int)(total >> 30),
			      (int)(total & ((1 << 30) - 1)) / 10737419);
	} else if (total > 1 << 20) {
		int x = total + 5243;  /* for rounding */
		l -= snprintf(tp->display, l, ", %u.%2.2u MiB",
			      x >> 20, ((x & ((1 << 20) - 1)) * 100) >> 20);
	} else if (total > 1 << 10) {
		int x = total + 5;  /* for rounding */
		l -= snprintf(tp->display, l, ", %u.%2.2u KiB",
			      x >> 10, ((x & ((1 << 10) - 1)) * 100) >> 10);
	} else {
		l -= snprintf(tp->display, l, ", %u bytes", (int)total);
	}

	if (rate > 1 << 10) {
		int x = rate + 5;  /* for rounding */
		snprintf(tp->display + sizeof(tp->display) - l, l,
			 " | %u.%2.2u MiB/s",
			 x >> 10, ((x & ((1 << 10) - 1)) * 100) >> 10);
	} else if (rate)
		snprintf(tp->display + sizeof(tp->display) - l, l,
			 " | %u KiB/s", rate);
}

void display_throughput(struct progress *progress, off_t total)
{
	struct throughput *tp;
	struct timeval tv;
	unsigned int misecs;

	if (!progress)
		return;
	tp = progress->throughput;

	gettimeofday(&tv, NULL);

	if (!tp) {
		progress->throughput = tp = calloc(1, sizeof(*tp));
		if (tp) {
			tp->prev_total = tp->curr_total = total;
			tp->prev_tv = tv;
		}
		return;
	}
	tp->curr_total = total;

	/*
	 * We have x = bytes and y = microsecs.  We want z = KiB/s:
	 *
	 *	z = (x / 1024) / (y / 1000000)
	 *	z = x / y * 1000000 / 1024
	 *	z = x / (y * 1024 / 1000000)
	 *	z = x / y'
	 *
	 * To simplify things we'll keep track of misecs, or 1024th of a sec
	 * obtained with:
	 *
	 *	y' = y * 1024 / 1000000
	 *	y' = y / (1000000 / 1024)
	 *	y' = y / 977
	 */
	misecs = (tv.tv_sec - tp->prev_tv.tv_sec) * 1024;
	misecs += (int)(tv.tv_usec - tp->prev_tv.tv_usec) / 977;

	if (misecs > 512) {
		unsigned int count, rate;

		count = total - tp->prev_total;
		tp->prev_total = total;
		tp->prev_tv = tv;
		tp->avg_bytes += count;
		tp->avg_misecs += misecs;
		rate = tp->avg_bytes / tp->avg_misecs;
		tp->avg_bytes -= tp->last_bytes[tp->idx];
		tp->avg_misecs -= tp->last_misecs[tp->idx];
		tp->last_bytes[tp->idx] = count;
		tp->last_misecs[tp->idx] = misecs;
		tp->idx = (tp->idx + 1) % TP_IDX_MAX;

		throughput_string(tp, total, rate);
		if (progress->last_value != -1 && progress_update)
			display(progress, progress->last_value, NULL);
	}
}

int display_progress(struct progress *progress, unsigned n)
{
	return progress ? display(progress, n, NULL) : 0;
}

struct progress *start_progress_delay(const char *title, unsigned total,
				       unsigned percent_treshold, unsigned delay)
{
	struct progress *progress = malloc(sizeof(*progress));
	if (!progress) {
		/* unlikely, but here's a good fallback */
		fprintf(stderr, "%s...\n", title);
		fflush(stderr);
		return NULL;
	}
	progress->title = title;
	progress->total = total;
	progress->last_value = -1;
	progress->last_percent = -1;
	progress->delayed_percent_treshold = percent_treshold;
	progress->delay = delay;
	progress->throughput = NULL;
	set_progress_signal();
	return progress;
}

struct progress *start_progress(const char *title, unsigned total)
{
	return start_progress_delay(title, total, 0, 0);
}

void stop_progress(struct progress **p_progress)
{
	stop_progress_msg(p_progress, "done");
}

void stop_progress_msg(struct progress **p_progress, const char *msg)
{
	struct progress *progress = *p_progress;
	if (!progress)
		return;
	*p_progress = NULL;
	if (progress->last_value != -1) {
		/* Force the last update */
		char buf[128], *bufp;
		size_t len = strlen(msg) + 5;
		struct throughput *tp = progress->throughput;

		bufp = (len < sizeof(buf)) ? buf : xmalloc(len + 1);
		if (tp) {
			unsigned int rate = !tp->avg_misecs ? 0 :
					tp->avg_bytes / tp->avg_misecs;
			throughput_string(tp, tp->curr_total, rate);
		}
		progress_update = 1;
		sprintf(bufp, ", %s.\n", msg);
		display(progress, progress->last_value, bufp);
		if (buf != bufp)
			free(bufp);
	}
	clear_progress_signal();
	free(progress->throughput);
	free(progress);
}
