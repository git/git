#ifndef PROGRESS_H
#define PROGRESS_H

struct progress;

void display_throughput(struct progress *progress, uint64_t total);
int display_progress(struct progress *progress, uint64_t n);
struct progress *start_progress(const char *title, uint64_t total);
struct progress *start_delayed_progress(const char *title, uint64_t total);
void stop_progress(struct progress **progress);
void stop_progress_msg(struct progress **progress, const char *msg);

#endif
