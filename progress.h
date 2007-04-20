#ifndef __progress_h__
#define __progress_h__

struct progress {
	const char *prefix;
	unsigned total;
	unsigned last_percent;
};

int display_progress(struct progress *progress, unsigned n);
void start_progress(struct progress *progress, const char *title,
		    const char *prefix, unsigned total);
void stop_progress(struct progress *progress);

#endif
