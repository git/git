#ifndef EPOCH_H
#define EPOCH_H


// return codes for emitter_func
#define STOP     0
#define CONTINUE 1
#define DO       2
typedef int (*emitter_func) (struct commit *); 

int sort_list_in_merge_order(struct commit_list *list, emitter_func emitter);

/* Low bits are used by rev-list */
#define UNINTERESTING   (1u<<10)
#define BOUNDARY        (1u<<11)
#define VISITED         (1u<<12)
#define DISCONTINUITY   (1u<<13)
#define LAST_EPOCH_FLAG (1u<<14)


#endif	/* EPOCH_H */
