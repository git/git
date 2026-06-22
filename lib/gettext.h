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
extern int git_gettext_enabled;
void git_setup_gettext(void);
int gettext_width(const char *s);
#else
#define git_gettext_enabled (0)
static inline void git_setup_gettext(void)
{
}
static inline int gettext_width(const char *s)
{
	return strlen(s);
}
#endif

static inline FORMAT_PRESERVING(1) const char *_(const char *msgid)
{
	if (!*msgid)
		return "";
	if (!git_gettext_enabled)
		return msgid;
	return gettext(msgid);
}

static inline FORMAT_PRESERVING(1) FORMAT_PRESERVING(2)
const char *Q_(const char *msgid, const char *plu, unsigned long n)
{
	if (!git_gettext_enabled)
		return n == 1 ? msgid : plu;
	return ngettext(msgid, plu, n);
}

/* Mark msgid for translation but do not translate it. */
#define N_(msgid) msgid

const char *get_preferred_languages(void);
int is_utf8_locale(void);

#endif
