#ifndef EPOCH_H
#define EPOCH_H


// return codes for emitter_func
#define STOP     0
#define CONTINUE 1
#define DO       2
typedef int (*emitter_func) (struct commit *); 

int sort_list_in_merge_order(struct commit_list *list, emitter_func emitter);

#define UNINTERESTING   (1u<<2)
#define BOUNDARY        (1u<<3)
#define VISITED         (1u<<4)
#define DISCONTINUITY   (1u<<5)
#define DUPCHECK        (1u<<6)
#define LAST_EPOCH_FLAG (1u<<6)


#endif	/* EPOCH_H */
