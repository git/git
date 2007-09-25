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
 * Returns 0 on a successful substitution pass that fits in result,
 * Returns a number of bytes needed to hold the full substituted
 * string otherwise.
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

        memset(result, 0, reslen);

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
				if (newlen + valuelen + 1 < reslen) {
					/* Substitute. */
					strncpy(dest, value, valuelen);
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

	if (newlen + 1 < reslen)
		return 0;
	else
		return newlen + 2;
}
