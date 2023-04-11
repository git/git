/*
 * Simple text-based progress display module for GIT
 *
 * Copyright (c) 2007 by Nicolas Pitre <nico@fluxnic.net>
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define GIT_TEST_PROGRESS_ONLY
#include "cache.h"
#include "pager.h"
#include "progress.h"
#include "strbuf.h"
#include "trace.h"
#include "trace2.h"
#include "utf8.h"
#include "config.h"

#define TP_IDX_MAX      8

struct throughput {
	off_t curr_total;
	off_t prev_total;
	uint64_t prev_ns;
	unsigned int avg_bytes;
	unsigned int avg_misecs;
	unsigned int last_bytes[TP_IDX_MAX];
	unsigned int last_misecs[TP_IDX_MAX];
	unsigned int idx;
	struct strbuf display;
};

struct progress {
	const char *title;
	uint64_t last_value;
	uint64_t total;
	unsigned last_percent;
	unsigned delay;
	unsigned sparse;
	struct throughput *throughput;
	uint64_t start_ns;
	struct strbuf counters_sb;
	int title_len;
	int split;
};

static volatile sig_atomic_t progress_update;

/*
 * These are only intended for testing the progress output, i.e. exclusively
 * for 'test-tool progress'.
 */
int progress_testing;
uint64_t progress_test_ns = 0;
void progress_test_force_update(void)
{
	progress_update = 1;
}


static void progress_interval(int signum UNUSED)
{
	progress_update = 1;
}

static void set_progress_signal(void)
{
	struct sigaction sa;
	struct itimerval v;

	if (progress_testing)
		return;

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

	if (progress_testing)
		return;

	setitimer(ITIMER_REAL, &v, NULL);
	signal(SIGALRM, SIG_IGN);
	progress_update = 0;
}

static int is_foreground_fd(int fd)
{
	int tpgrp = tcgetpgrp(fd);
	return tpgrp < 0 || tpgrp == getpgid(0);
}

static void display(struct progress *progress, uint64_t n, const char *done)
{
	const char *tp;
	struct strbuf *counters_sb = &progress->counters_sb;
	int show_update = 0;
	int last_count_len = counters_sb->len;

	if (progress->delay && (!progress_update || --progress->delay))
		return;

	progress->last_value = n;
	tp = (progress->throughput) ? progress->throughput->display.buf : "";
	if (progress->total) {
		unsigned percent = n * 100 / progress->total;
		if (percent != progress->last_percent || progress_update) {
			progress->last_percent = percent;

			strbuf_reset(counters_sb);
			strbuf_addf(counters_sb,
				    "%3u%% (%"PRIuMAX"/%"PRIuMAX")%s", percent,
				    (uintmax_t)n, (uintmax_t)progress->total,
				    tp);
			show_update = 1;
		}
	} else if (progress_update) {
		strbuf_reset(counters_sb);
		strbuf_addf(counters_sb, "%"PRIuMAX"%s", (uintmax_t)n, tp);
		show_update = 1;
	}

	if (show_update) {
		if (is_foreground_fd(fileno(stderr)) || done) {
			const char *eol = done ? done : "\r";
			size_t clear_len = counters_sb->len < last_count_len ?
					last_count_len - counters_sb->len + 1 :
					0;
			/* The "+ 2" accounts for the ": ". */
			size_t progress_line_len = progress->title_len +
						counters_sb->len + 2;
			int cols = term_columns();

			if (progress->split) {
				fprintf(stderr, "  %s%*s", counters_sb->buf,
					(int) clear_len, eol);
			} else if (!done && cols < progress_line_len) {
				clear_len = progress->title_len + 1 < cols ?
					    cols - progress->title_len - 1 : 0;
				fprintf(stderr, "%s:%*s\n  %s%s",
					progress->title, (int) clear_len, "",
					counters_sb->buf, eol);
				progress->split = 1;
			} else {
				fprintf(stderr, "%s: %s%*s", progress->title,
					counters_sb->buf, (int) clear_len, eol);
			}
			fflush(stderr);
		}
		progress_update = 0;
	}
}

static void throughput_string(struct strbuf *buf, uint64_t total,
			      unsigned int rate)
{
	strbuf_reset(buf);
	strbuf_addstr(buf, ", ");
	strbuf_humanise_bytes(buf, total);
	strbuf_addstr(buf, " | ");
	strbuf_humanise_rate(buf, rate * 1024);
}

static uint64_t progress_getnanotime(struct progress *progress)
{
	if (progress_testing)
		return progress->start_ns + progress_test_ns;
	else
		return getnanotime();
}

void display_throughput(struct progress *progress, uint64_t total)
{
	struct throughput *tp;
	uint64_t now_ns;
	unsigned int misecs, count, rate;

	if (!progress)
		return;
	tp = progress->throughput;

	now_ns = progress_getnanotime(progress);

	if (!tp) {
		progress->throughput = CALLOC_ARRAY(tp, 1);
		tp->prev_total = tp->curr_total = total;
		tp->prev_ns = now_ns;
		strbuf_init(&tp->display, 0);
		return;
	}
	tp->curr_total = total;

	/* only update throughput every 0.5 s */
	if (now_ns - tp->prev_ns <= 500000000)
		return;

	/*
	 * We have x = bytes and y = nanosecs.  We want z = KiB/s:
	 *
	 *	z = (x / 1024) / (y / 1000000000)
	 *	z = x / y * 1000000000 / 1024
	 *	z = x / (y * 1024 / 1000000000)
	 *	z = x / y'
	 *
	 * To simplify things we'll keep track of misecs, or 1024th of a sec
	 * obtained with:
	 *
	 *	y' = y * 1024 / 1000000000
	 *	y' = y * (2^10 / 2^42) * (2^42 / 1000000000)
	 *	y' = y / 2^32 * 4398
	 *	y' = (y * 4398) >> 32
	 */
	misecs = ((now_ns - tp->prev_ns) * 4398) >> 32;

	count = total - tp->prev_total;
	tp->prev_total = total;
	tp->prev_ns = now_ns;
	tp->avg_bytes += count;
	tp->avg_misecs += misecs;
	rate = tp->avg_bytes / tp->avg_misecs;
	tp->avg_bytes -= tp->last_bytes[tp->idx];
	tp->avg_misecs -= tp->last_misecs[tp->idx];
	tp->last_bytes[tp->idx] = count;
	tp->last_misecs[tp->idx] = misecs;
	tp->idx = (tp->idx + 1) % TP_IDX_MAX;

	throughput_string(&tp->display, total, rate);
	if (progress->last_value != -1 && progress_update)
		display(progress, progress->last_value, NULL);
}

void display_progress(struct progress *progress, uint64_t n)
{
	if (progress)
		display(progress, n, NULL);
}

static struct progress *start_progress_delay(const char *title, uint64_t total,
					     unsigned delay, unsigned sparse)
{
	struct progress *progress = xmalloc(sizeof(*progress));
	progress->title = title;
	progress->total = total;
	progress->last_value = -1;
	progress->last_percent = -1;
	progress->delay = delay;
	progress->sparse = sparse;
	progress->throughput = NULL;
	progress->start_ns = getnanotime();
	strbuf_init(&progress->counters_sb, 0);
	progress->title_len = utf8_strwidth(title);
	progress->split = 0;
	set_progress_signal();
	trace2_region_enter("progress", title, the_repository);
	return progress;
}

static int get_default_delay(void)
{
	static int delay_in_secs = -1;

	if (delay_in_secs < 0)
		delay_in_secs = git_env_ulong("GIT_PROGRESS_DELAY", 2);

	return delay_in_secs;
}

struct progress *start_delayed_progress(const char *title, uint64_t total)
{
	return start_progress_delay(title, total, get_default_delay(), 0);
}

struct progress *start_progress(const char *title, uint64_t total)
{
	return start_progress_delay(title, total, 0, 0);
}

/*
 * Here "sparse" means that the caller might use some sampling criteria to
 * decide when to call display_progress() rather than calling it for every
 * integer value in[0 .. total).  In particular, the caller might not call
 * display_progress() for the last value in the range.
 *
 * When "sparse" is set, stop_progress() will automatically force the done
 * message to show 100%.
 */
struct progress *start_sparse_progress(const char *title, uint64_t total)
{
	return start_progress_delay(title, total, 0, 1);
}

struct progress *start_delayed_sparse_progress(const char *title,
					       uint64_t total)
{
	return start_progress_delay(title, total, get_default_delay(), 1);
}

static void finish_if_sparse(struct progress *progress)
{
	if (progress->sparse &&
	    progress->last_value != progress->total)
		display_progress(progress, progress->total);
}

static void force_last_update(struct progress *progress, const char *msg)
{
	char *buf;
	struct throughput *tp = progress->throughput;

	if (tp) {
		uint64_t now_ns = progress_getnanotime(progress);
		unsigned int misecs, rate;
		misecs = ((now_ns - progress->start_ns) * 4398) >> 32;
		rate = tp->curr_total / (misecs ? misecs : 1);
		throughput_string(&tp->display, tp->curr_total, rate);
	}
	progress_update = 1;
	buf = xstrfmt(", %s.\n", msg);
	display(progress, progress->last_value, buf);
	free(buf);
}

static void log_trace2(struct progress *progress)
{
	trace2_data_intmax("progress", the_repository, "total_objects",
			   progress->total);

	if (progress->throughput)
		trace2_data_intmax("progress", the_repository, "total_bytes",
				   progress->throughput->curr_total);

	trace2_region_leave("progress", progress->title, the_repository);
}

void stop_progress_msg(struct progress **p_progress, const char *msg)
{
	struct progress *progress;

	if (!p_progress)
		BUG("don't provide NULL to stop_progress_msg");

	progress = *p_progress;
	if (!progress)
		return;
	*p_progress = NULL;

	finish_if_sparse(progress);
	if (progress->last_value != -1)
		force_last_update(progress, msg);
	log_trace2(progress);

	clear_progress_signal();
	strbuf_release(&progress->counters_sb);
	if (progress->throughput)
		strbuf_release(&progress->throughput->display);
	free(progress->throughput);
	free(progress);
}
