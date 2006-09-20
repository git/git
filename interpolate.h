/*
 * Copyright 2006 Jon Loeliger
 */

#ifndef INTERPOLATE_H
#define INTERPOLATE_H


struct interp {
	char *name;
	char *value;
};

extern int interpolate(char *result, int reslen,
		       char *orig,
		       struct interp *interps, int ninterps);

#endif /* INTERPOLATE_H */
