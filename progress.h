#ifndef PROGRESS_H
#define PROGRESS_H

struct progress;

void display_throughput(struct progress *progress, off_t total);
int display_progress(struct progress *progress, unsigned n);
struct progress *start_progress(const char *title, unsigned total, const char *eol);
struct progress *start_progress_delay(const char *title, unsigned total,
				       unsigned percent_treshold, unsigned delay,
				       const char *eol);
void stop_progress(struct progress **progress);
void stop_progress_msg(struct progress **progress, const char *msg);

#endif
