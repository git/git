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

#define FORMAT_PRESERVING(n) __attribute__((format_arg(n)))

#ifdef GETTEXT_POISON
extern int use_gettext_poison(void);
#else
#define use_gettext_poison() 0
#endif

static inline FORMAT_PRESERVING(1) const char *_(const char *msgid)
{
	return use_gettext_poison() ? "# GETTEXT POISON #" : msgid;
}

static inline FORMAT_PRESERVING(1) FORMAT_PRESERVING(2)
const char *Q_(const char *msgid, const char *plu, unsigned long n)
{
	if (use_gettext_poison())
		return "# GETTEXT POISON #";
	return n == 1 ? msgid : plu;
}

/* Mark msgid for translation but do not translate it. */
#define N_(msgid) msgid

#endif
