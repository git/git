/*
 * Copyright 2006 Jon Loeliger
 */

#include "git-compat-util.h"
#include "interpolate.h"


void interp_set_entry(struct interp *table, int slot, const char *value)
{
	char *oldval = table[slot].value;
	char *newval = NULL;

	free(oldval);

	if (value)
		newval = xstrdup(value);

	table[slot].value = newval;
}


void interp_clear_table(struct interp *table, int ninterps)
{
	int i;

	for (i = 0; i < ninterps; i++) {
		interp_set_entry(table, i, NULL);
	}
}


/*
 * Convert a NUL-terminated string in buffer orig
 * into the supplied buffer, result, whose length is reslen,
 * performing substitutions on %-named sub-strings from
 * the table, interps, with ninterps entries.
 *
 * Example interps:
 *    {
 *        { "%H", "example.org"},
 *        { "%port", "123"},
 *        { "%%", "%"},
 *    }
 *
 * Returns the length of the substituted string (not including the final \0).
 * Like with snprintf, if the result is >= reslen, then it overflowed.
 */

unsigned long interpolate(char *result, unsigned long reslen,
		const char *orig,
		const struct interp *interps, int ninterps)
{
	const char *src = orig;
	char *dest = result;
	unsigned long newlen = 0;
	const char *name, *value;
	unsigned long namelen, valuelen;
	int i;
	char c;

	while ((c = *src)) {
		if (c == '%') {
			/* Try to match an interpolation string. */
			for (i = 0; i < ninterps; i++) {
				name = interps[i].name;
				namelen = strlen(name);
				if (strncmp(src, name, namelen) == 0)
					break;
			}

			/* Check for valid interpolation. */
			if (i < ninterps) {
				value = interps[i].value;
				if (!value) {
					src += namelen;
					continue;
				}

				valuelen = strlen(value);
				if (newlen + valuelen < reslen) {
					/* Substitute. */
					memcpy(dest, value, valuelen);
					dest += valuelen;
				}
				newlen += valuelen;
				src += namelen;
				continue;
			}
		}
		/* Straight copy one non-interpolation character. */
		if (newlen + 1 < reslen)
			*dest++ = *src;
		src++;
		newlen++;
	}

	/* XXX: the previous loop always keep room for the ending NUL,
	   we just need to check if there was room for a NUL in the first place */
	if (reslen > 0)
		*dest = '\0';
	return newlen;
}
