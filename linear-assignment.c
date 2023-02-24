/*
 * Based on: Jonker, R., & Volgenant, A. (1987). <i>A shortest augmenting path
 * algorithm for dense and sparse linear assignment problems</i>. Computing,
 * 38(4), 325-340.
 */
#include "git-compat-util.h"
#include "linear-assignment.h"

#define COST(column, row) cost[(column) + column_count * (row)]

/*
 * The parameter `cost` is the cost matrix: the cost to assign column j to row
 * i is `cost[j + column_count * i].
 */
void compute_assignment(int column_count, int row_count, int *cost,
			int *column2row, int *row2column)
{
	int *v, *d;
	int *free_row, free_count = 0, saved_free_count, *pred, *col;
	int i, j, phase;

	if (column_count < 2) {
		memset(column2row, 0, sizeof(int) * column_count);
		memset(row2column, 0, sizeof(int) * row_count);
		return;
	}

	memset(column2row, -1, sizeof(int) * column_count);
	memset(row2column, -1, sizeof(int) * row_count);
	ALLOC_ARRAY(v, column_count);

	/* column reduction */
	for (j = column_count - 1; j >= 0; j--) {
		int i1 = 0;

		for (i = 1; i < row_count; i++)
			if (COST(j, i1) > COST(j, i))
				i1 = i;
		v[j] = COST(j, i1);
		if (row2column[i1] == -1) {
			/* row i1 unassigned */
			row2column[i1] = j;
			column2row[j] = i1;
		} else {
			if (row2column[i1] >= 0)
				row2column[i1] = -2 - row2column[i1];
			column2row[j] = -1;
		}
	}

	/* reduction transfer */
	ALLOC_ARRAY(free_row, row_count);
	for (i = 0; i < row_count; i++) {
		int j1 = row2column[i];
		if (j1 == -1)
			free_row[free_count++] = i;
		else if (j1 < -1)
			row2column[i] = -2 - j1;
		else {
			int min = COST(!j1, i) - v[!j1];
			for (j = 1; j < column_count; j++)
				if (j != j1 && min > COST(j, i) - v[j])
					min = COST(j, i) - v[j];
			v[j1] -= min;
		}
	}

	if (free_count ==
	    (column_count < row_count ? row_count - column_count : 0)) {
		free(v);
		free(free_row);
		return;
	}

	/* augmenting row reduction */
	for (phase = 0; phase < 2; phase++) {
		int k = 0;

		saved_free_count = free_count;
		free_count = 0;
		while (k < saved_free_count) {
			int u1, u2;
			int j1 = 0, j2, i0;

			i = free_row[k++];
			u1 = COST(j1, i) - v[j1];
			j2 = -1;
			u2 = INT_MAX;
			for (j = 1; j < column_count; j++) {
				int c = COST(j, i) - v[j];
				if (u2 > c) {
					if (u1 < c) {
						u2 = c;
						j2 = j;
					} else {
						u2 = u1;
						u1 = c;
						j2 = j1;
						j1 = j;
					}
				}
			}
			if (j2 < 0) {
				j2 = j1;
				u2 = u1;
			}

			i0 = column2row[j1];
			if (u1 < u2)
				v[j1] -= u2 - u1;
			else if (i0 >= 0) {
				j1 = j2;
				i0 = column2row[j1];
			}

			if (i0 >= 0) {
				if (u1 < u2)
					free_row[--k] = i0;
				else
					free_row[free_count++] = i0;
			}
			row2column[i] = j1;
			column2row[j1] = i;
		}
	}

	/* augmentation */
	saved_free_count = free_count;
	ALLOC_ARRAY(d, column_count);
	ALLOC_ARRAY(pred, column_count);
	ALLOC_ARRAY(col, column_count);
	for (free_count = 0; free_count < saved_free_count; free_count++) {
		int i1 = free_row[free_count], low = 0, up = 0, last, k;
		int min, c, u1;

		for (j = 0; j < column_count; j++) {
			d[j] = COST(j, i1) - v[j];
			pred[j] = i1;
			col[j] = j;
		}

		j = -1;
		do {
			last = low;
			min = d[col[up++]];
			for (k = up; k < column_count; k++) {
				j = col[k];
				c = d[j];
				if (c <= min) {
					if (c < min) {
						up = low;
						min = c;
					}
					col[k] = col[up];
					col[up++] = j;
				}
			}
			for (k = low; k < up; k++)
				if (column2row[col[k]] == -1)
					goto update;

			/* scan a row */
			do {
				int j1 = col[low++];

				i = column2row[j1];
				u1 = COST(j1, i) - v[j1] - min;
				for (k = up; k < column_count; k++) {
					j = col[k];
					c = COST(j, i) - v[j] - u1;
					if (c < d[j]) {
						d[j] = c;
						pred[j] = i;
						if (c == min) {
							if (column2row[j] == -1)
								goto update;
							col[k] = col[up];
							col[up++] = j;
						}
					}
				}
			} while (low != up);
		} while (low == up);

update:
		/* updating of the column pieces */
		for (k = 0; k < last; k++) {
			int j1 = col[k];
			v[j1] += d[j1] - min;
		}

		/* augmentation */
		do {
			if (j < 0)
				BUG("negative j: %d", j);
			i = pred[j];
			column2row[j] = i;
			SWAP(j, row2column[i]);
		} while (i1 != i);
	}

	free(col);
	free(pred);
	free(d);
	free(v);
	free(free_row);
}
