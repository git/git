#ifndef PROGRESS_H
#define PROGRESS_H

struct progress {
	const char *prefix;
	unsigned total;
	unsigned last_percent;
	unsigned delay;
	unsigned delayed_percent_treshold;
	const char *delayed_title;
};

int display_progress(struct progress *progress, unsigned n);
void start_progress(struct progress *progress, const char *title,
		    const char *prefix, unsigned total);
void start_progress_delay(struct progress *progress, const char *title,
			  const char *prefix, unsigned total,
			  unsigned percent_treshold, unsigned delay);
void stop_progress(struct progress *progress);

#endif
