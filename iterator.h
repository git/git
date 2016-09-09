#ifndef ITERATOR_H
#define ITERATOR_H

/*
 * Generic constants related to iterators.
 */

/*
 * The attempt to advance the iterator was successful; the iterator
 * reflects the new current entry.
 */
#define ITER_OK 0

/*
 * The iterator is exhausted and has been freed.
 */
#define ITER_DONE -1

/*
 * The iterator experienced an error. The iteration has been aborted
 * and the iterator has been freed.
 */
#define ITER_ERROR -2

/*
 * Return values for selector functions for merge iterators. The
 * numerical values of these constants are important and must be
 * compatible with ITER_DONE and ITER_ERROR.
 */
enum iterator_selection {
	/* End the iteration without an error: */
	ITER_SELECT_DONE = ITER_DONE,

	/* Report an error and abort the iteration: */
	ITER_SELECT_ERROR = ITER_ERROR,

	/*
	 * The next group of constants are masks that are useful
	 * mainly internally.
	 */

	/* The LSB selects whether iter0/iter1 is the "current" iterator: */
	ITER_CURRENT_SELECTION_MASK = 0x01,

	/* iter0 is the "current" iterator this round: */
	ITER_CURRENT_SELECTION_0 = 0x00,

	/* iter1 is the "current" iterator this round: */
	ITER_CURRENT_SELECTION_1 = 0x01,

	/* Yield the value from the current iterator? */
	ITER_YIELD_CURRENT = 0x02,

	/* Discard the value from the secondary iterator? */
	ITER_SKIP_SECONDARY = 0x04,

	/*
	 * The constants that a selector function should usually
	 * return.
	 */

	/* Yield the value from iter0: */
	ITER_SELECT_0 = ITER_CURRENT_SELECTION_0 | ITER_YIELD_CURRENT,

	/* Yield the value from iter0 and discard the one from iter1: */
	ITER_SELECT_0_SKIP_1 = ITER_SELECT_0 | ITER_SKIP_SECONDARY,

	/* Discard the value from iter0 without yielding anything this round: */
	ITER_SKIP_0 = ITER_CURRENT_SELECTION_1 | ITER_SKIP_SECONDARY,

	/* Yield the value from iter1: */
	ITER_SELECT_1 = ITER_CURRENT_SELECTION_1 | ITER_YIELD_CURRENT,

	/* Yield the value from iter1 and discard the one from iter0: */
	ITER_SELECT_1_SKIP_0 = ITER_SELECT_1 | ITER_SKIP_SECONDARY,

	/* Discard the value from iter1 without yielding anything this round: */
	ITER_SKIP_1 = ITER_CURRENT_SELECTION_0 | ITER_SKIP_SECONDARY
};

#endif /* ITERATOR_H */
