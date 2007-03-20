/*
 * Copyright 2006 Jon Loeliger
 */

#include "git-compat-util.h"
#include "interpolate.h"


void interp_set_entry(struct interp *table, int slot, const char *value)
{
	char *oldval = table[slot].value;
	char *newval = NULL;

	if (oldval)
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
 * Returns 1 on a successful substitution pass that fits in result,
 * Returns 0 on a failed or overflowing substitution pass.
 */

int interpolate(char *result, int reslen,
		const char *orig,
		const struct interp *interps, int ninterps)
{
	const char *src = orig;
	char *dest = result;
	int newlen = 0;
	const char *name, *value;
	int namelen, valuelen;
	int i;
	char c;

        memset(result, 0, reslen);

	while ((c = *src) && newlen < reslen - 1) {
		if (c == '%') {
			/* Try to match an interpolation string. */
			for (i = 0; i < ninterps; i++) {
				name = interps[i].name;
				namelen = strlen(name);
				if (strncmp(src, name, namelen) == 0) {
					break;
				}
			}

			/* Check for valid interpolation. */
			if (i < ninterps) {
				value = interps[i].value;
				valuelen = strlen(value);

				if (newlen + valuelen < reslen - 1) {
					/* Substitute. */
					strncpy(dest, value, valuelen);
					newlen += valuelen;
					dest += valuelen;
					src += namelen;
				} else {
					/* Something's not fitting. */
					return 0;
				}

			} else {
				/* Skip bogus interpolation. */
				*dest++ = *src++;
				newlen++;
			}

		} else {
			/* Straight copy one non-interpolation character. */
			*dest++ = *src++;
			newlen++;
		}
	}

	return newlen < reslen - 1;
}
