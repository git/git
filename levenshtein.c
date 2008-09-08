#include "cache.h"
#include "levenshtein.h"

int levenshtein(const char *string1, const char *string2,
		int w, int s, int a, int d)
{
	int len1 = strlen(string1), len2 = strlen(string2);
	int *row0 = xmalloc(sizeof(int) * (len2 + 1));
	int *row1 = xmalloc(sizeof(int) * (len2 + 1));
	int *row2 = xmalloc(sizeof(int) * (len2 + 1));
	int i, j;

	for (j = 0; j <= len2; j++)
		row1[j] = j * a;
	for (i = 0; i < len1; i++) {
		int *dummy;

		row2[0] = (i + 1) * d;
		for (j = 0; j < len2; j++) {
			/* substitution */
			row2[j + 1] = row1[j] + s * (string1[i] != string2[j]);
			/* swap */
			if (i > 0 && j > 0 && string1[i - 1] == string2[j] &&
					string1[i] == string2[j - 1] &&
					row2[j + 1] > row0[j - 1] + w)
				row2[j + 1] = row0[j - 1] + w;
			/* deletion */
			if (j + 1 < len2 && row2[j + 1] > row1[j + 1] + d)
				row2[j + 1] = row1[j + 1] + d;
			/* insertion */
			if (row2[j + 1] > row2[j] + a)
				row2[j + 1] = row2[j] + a;
		}

		dummy = row0;
		row0 = row1;
		row1 = row2;
		row2 = dummy;
	}

	i = row1[len2];
	free(row0);
	free(row1);
	free(row2);

	return i;
}
