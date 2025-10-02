#include "git-compat-util.h"
#include "prio-queue.h"

static inline int compare(struct prio_queue *queue, size_t i, size_t j)
{
	int cmp = queue->compare(queue->array[i].data, queue->array[j].data,
				 queue->cb_data);
	if (!cmp)
		cmp = (queue->array[i].ctr > queue->array[j].ctr) -
		      (queue->array[i].ctr < queue->array[j].ctr);
	return cmp;
}

static inline void swap(struct prio_queue *queue, size_t i, size_t j)
{
	SWAP(queue->array[i], queue->array[j]);
}

void prio_queue_reverse(struct prio_queue *queue)
{
	size_t i, j;

	if (queue->compare)
		BUG("prio_queue_reverse() on non-LIFO queue");
	if (!queue->nr)
		return;
	for (i = 0; i < (j = (queue->nr - 1) - i); i++)
		swap(queue, i, j);
}

void clear_prio_queue(struct prio_queue *queue)
{
	FREE_AND_NULL(queue->array);
	queue->nr = 0;
	queue->alloc = 0;
	queue->insertion_ctr = 0;
}

void prio_queue_put(struct prio_queue *queue, void *thing)
{
	size_t ix, parent;

	/* Append at the end */
	ALLOC_GROW(queue->array, queue->nr + 1, queue->alloc);
	queue->array[queue->nr].ctr = queue->insertion_ctr++;
	queue->array[queue->nr].data = thing;
	queue->nr++;
	if (!queue->compare)
		return; /* LIFO */

	/* Bubble up the new one */
	for (ix = queue->nr - 1; ix; ix = parent) {
		parent = (ix - 1) / 2;
		if (compare(queue, parent, ix) <= 0)
			break;

		swap(queue, parent, ix);
	}
}

static void sift_down_root(struct prio_queue *queue)
{
	size_t ix, child;

	/* Push down the one at the root */
	for (ix = 0; ix * 2 + 1 < queue->nr; ix = child) {
		child = ix * 2 + 1; /* left */
		if (child + 1 < queue->nr &&
		    compare(queue, child, child + 1) >= 0)
			child++; /* use right child */

		if (compare(queue, ix, child) <= 0)
			break;

		swap(queue, child, ix);
	}
}

void *prio_queue_get(struct prio_queue *queue)
{
	void *result;

	if (!queue->nr)
		return NULL;
	if (!queue->compare)
		return queue->array[--queue->nr].data; /* LIFO */

	result = queue->array[0].data;
	if (!--queue->nr)
		return result;

	queue->array[0] = queue->array[queue->nr];
	sift_down_root(queue);
	return result;
}

void *prio_queue_peek(struct prio_queue *queue)
{
	if (!queue->nr)
		return NULL;
	if (!queue->compare)
		return queue->array[queue->nr - 1].data;
	return queue->array[0].data;
}

void prio_queue_replace(struct prio_queue *queue, void *thing)
{
	if (!queue->nr) {
		prio_queue_put(queue, thing);
	} else if (!queue->compare) {
		queue->array[queue->nr - 1].ctr = queue->insertion_ctr++;
		queue->array[queue->nr - 1].data = thing;
	} else {
		queue->array[0].ctr = queue->insertion_ctr++;
		queue->array[0].data = thing;
		sift_down_root(queue);
	}
}
