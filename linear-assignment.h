#ifndef LINEAR_ASSIGNMENT_H
#define LINEAR_ASSIGNMENT_H

/*
 * Compute an assignment of columns -> rows (and vice versa) such that every
 * column is assigned to at most one row (and vice versa) minimizing the
 * overall cost.
 *
 * The parameter `cost` is the cost matrix: the cost to assign column j to row
 * i is `cost[j + column_count * i].
 *
 * The arrays column2row and row2column will be populated with the respective
 * assignments (-1 for unassigned, which can happen only if column_count !=
 * row_count).
 */
void compute_assignment(int column_count, int row_count, int *cost,
			int *column2row, int *row2column);

/* The maximal cost in the cost matrix (to prevent integer overflows). */
#define COST_MAX (1<<16)

#endif
