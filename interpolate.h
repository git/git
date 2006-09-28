/*
 * Copyright 2006 Jon Loeliger
 */

#ifndef INTERPOLATE_H
#define INTERPOLATE_H

/*
 * Convert a NUL-terminated string in buffer orig,
 * performing substitutions on %-named sub-strings from
 * the interpretation table.
 */

struct interp {
	char *name;
	char *value;
};

extern int interpolate(char *result, int reslen,
		       const char *orig,
		       const struct interp *interps, int ninterps);

#endif /* INTERPOLATE_H */
