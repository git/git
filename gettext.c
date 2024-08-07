/*
 * Copyright (c) 2010 Ævar Arnfjörð Bjarmason
 */

#include "git-compat-util.h"
#include "abspath.h"
#include "environment.h"
#include "exec-cmd.h"
#include "gettext.h"
#include "utf8.h"

#ifndef NO_GETTEXT
#	include <libintl.h>
#	ifdef GIT_WINDOWS_NATIVE

static const char *locale_charset(void)
{
	const char *env = getenv("LC_ALL"), *dot;

	if (!env || !*env)
		env = getenv("LC_CTYPE");
	if (!env || !*env)
		env = getenv("LANG");

	if (!env)
		return "UTF-8";

	dot = strchr(env, '.');
	return !dot ? env : dot + 1;
}

#	elif defined HAVE_LIBCHARSET_H
#		include <libcharset.h>
#	else
#		include <langinfo.h>
#		define locale_charset() nl_langinfo(CODESET)
#	endif
#endif

static const char *charset;

/*
 * Guess the user's preferred languages from the value in LANGUAGE environment
 * variable and LC_MESSAGES locale category if NO_GETTEXT is not defined.
 *
 * The result can be a colon-separated list like "ko:ja:en".
 */
const char *get_preferred_languages(void)
{
	const char *retval;

	retval = getenv("LANGUAGE");
	if (retval && *retval)
		return retval;

#ifndef NO_GETTEXT
	retval = setlocale(LC_MESSAGES, NULL);
	if (retval && *retval &&
		strcmp(retval, "C") &&
		strcmp(retval, "POSIX"))
		return retval;
#endif

	return NULL;
}

#ifndef NO_GETTEXT
__attribute__((format (printf, 1, 2)))
static int test_vsnprintf(const char *fmt, ...)
{
	char buf[26];
	int ret;
	va_list ap;
	va_start(ap, fmt);
	ret = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return ret;
}

static void init_gettext_charset(const char *domain)
{
	charset = locale_charset();
	bind_textdomain_codeset(domain, charset);

	/*
	 * Work around an old bug fixed in glibc 2.17 (released on
	 * 2012-12-24), at the cost of potentially making translated
	 * messages from external functions like perror() emitted in
	 * the wrong encoding.
	 *
	 * The bug affected e.g. git.git's own 7eb93c89651 ([PATCH]
	 * Simplify git script, 2005-09-07), which is the origin of
	 * the "David_K\345gedal" test string.
	 *
	 * See a much longer comment added to this file in 5e9637c6297
	 * (i18n: add infrastructure for translating Git with gettext,
	 * 2011-11-18) for more details.
	 */
	if (test_vsnprintf("%.*s", 13, "David_K\345gedal") < 0)
		setlocale(LC_CTYPE, "C");
}

int git_gettext_enabled = 0;

void git_setup_gettext(void)
{
	const char *podir = getenv(GIT_TEXT_DOMAIN_DIR_ENVIRONMENT);
	char *p = NULL;

	if (!podir)
		podir = p = system_path(GIT_LOCALE_PATH);

	if (!is_directory(podir)) {
		free(p);
		return;
	}

	bindtextdomain("git", podir);
	setlocale(LC_MESSAGES, "");
	setlocale(LC_TIME, "");
	init_gettext_charset("git");
	textdomain("git");

	git_gettext_enabled = 1;

	free(p);
}

/* return the number of columns of string 's' in current locale */
int gettext_width(const char *s)
{
	static int is_utf8 = -1;
	if (is_utf8 == -1)
		is_utf8 = is_utf8_locale();

	return is_utf8 ? utf8_strwidth(s) : strlen(s);
}
#endif

int is_utf8_locale(void)
{
#ifdef NO_GETTEXT
	if (!charset) {
		const char *env = getenv("LC_ALL");
		if (!env || !*env)
			env = getenv("LC_CTYPE");
		if (!env || !*env)
			env = getenv("LANG");
		if (!env)
			env = "";
		if (strchr(env, '.'))
			env = strchr(env, '.') + 1;
		charset = xstrdup(env);
	}
#endif
	return is_encoding_utf8(charset);
}
