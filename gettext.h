/*
 * Copyright (c) 2010-2011 Ævar Arnfjörð Bjarmason
 *
 * This is a skeleton no-op implementation of gettext for Git.
 * You can replace it with something that uses libintl.h and wraps
 * gettext() to try out the translations.
 */

#ifndef GETTEXT_H
#define GETTEXT_H

#if defined(_) || defined(Q_)
#error "namespace conflict: '_' or 'Q_' is pre-defined?"
#endif

#ifndef NO_GETTEXT
#	include <libintl.h>
#else
#	ifdef gettext
#		undef gettext
#	endif
#	define gettext(s) (s)
#	ifdef ngettext
#		undef ngettext
#	endif
#	define ngettext(s, p, n) ((n == 1) ? (s) : (p))
#endif

#define FORMAT_PRESERVING(n) __attribute__((format_arg(n)))

#ifndef NO_GETTEXT
extern void git_setup_gettext(void);
#else
static inline void git_setup_gettext(void)
{
}
#endif

#ifdef GETTEXT_POISON
extern int use_gettext_poison(void);
#else
#define use_gettext_poison() 0
#endif

static inline FORMAT_PRESERVING(1) const char *_(const char *msgid)
{
	return use_gettext_poison() ? "# GETTEXT POISON #" : gettext(msgid);
}

static inline FORMAT_PRESERVING(1) FORMAT_PRESERVING(2)
const char *Q_(const char *msgid, const char *plu, unsigned long n)
{
	if (use_gettext_poison())
		return "# GETTEXT POISON #";
	return ngettext(msgid, plu, n);
}

/* Mark msgid for translation but do not translate it. */
#define N_(msgid) msgid

#endif
