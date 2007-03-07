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
	const char *name;
	char *value;
};

extern void interp_set_entry(struct interp *table, int slot, const char *value);
extern void interp_clear_table(struct interp *table, int ninterps);

extern int interpolate(char *result, int reslen,
		       const char *orig,
		       const struct interp *interps, int ninterps);

#endif /* INTERPOLATE_H */
