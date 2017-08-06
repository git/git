/*
 * Copyright (c) 2010 Ævar Arnfjörð Bjarmason
 */

#include "git-compat-util.h"
#include "gettext.h"
#include "strbuf.h"
#include "utf8.h"
#include "cache.h"
#include "exec_cmd.h"

#ifndef NO_GETTEXT
#	include <locale.h>
#	include <libintl.h>
#	ifdef GIT_WINDOWS_NATIVE
#		define locale_charset() "UTF-8"
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

#ifdef GETTEXT_POISON
int use_gettext_poison(void)
{
	static int poison_requested = -1;
	if (poison_requested == -1)
		poison_requested = getenv("GIT_GETTEXT_POISON") ? 1 : 0;
	return poison_requested;
}
#endif

#ifndef NO_GETTEXT
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
	/*
	   This trick arranges for messages to be emitted in the user's
	   requested encoding, but avoids setting LC_CTYPE from the
	   environment for the whole program.

	   This primarily done to avoid a bug in vsnprintf in the GNU C
	   Library [1]. which triggered a "your vsnprintf is broken" error
	   on Git's own repository when inspecting v0.99.6~1 under a UTF-8
	   locale.

	   That commit contains a ISO-8859-1 encoded author name, which
	   the locale aware vsnprintf(3) won't interpolate in the format
	   argument, due to mismatch between the data encoding and the
	   locale.

	   Even if it wasn't for that bug we wouldn't want to use LC_CTYPE at
	   this point, because it'd require auditing all the code that uses C
	   functions whose semantics are modified by LC_CTYPE.

	   But only setting LC_MESSAGES as we do creates a problem, since
	   we declare the encoding of our PO files[2] the gettext
	   implementation will try to recode it to the user's locale, but
	   without LC_CTYPE it'll emit something like this on 'git init'
	   under the Icelandic locale:

	       Bj? til t?ma Git lind ? /hlagh/.git/

	   Gettext knows about the encoding of our PO file, but we haven't
	   told it about the user's encoding, so all the non-US-ASCII
	   characters get encoded to question marks.

	   But we're in luck! We can set LC_CTYPE from the environment
	   only while we call nl_langinfo and
	   bind_textdomain_codeset. That suffices to tell gettext what
	   encoding it should emit in, so it'll now say:

	       Bjó til tóma Git lind í /hlagh/.git/

	   And the equivalent ISO-8859-1 string will be emitted under a
	   ISO-8859-1 locale.

	   With this change way we get the advantages of setting LC_CTYPE
	   (talk to the user in his language/encoding), without the major
	   drawbacks (changed semantics for C functions we rely on).

	   However foreign functions using other message catalogs that
	   aren't using our neat trick will still have a problem, e.g. if
	   we have to call perror(3):

	   #include <stdio.h>
	   #include <locale.h>
	   #include <errno.h>

	   int main(void)
	   {
		   setlocale(LC_MESSAGES, "");
		   setlocale(LC_CTYPE, "C");
		   errno = ENODEV;
		   perror("test");
		   return 0;
	   }

	   Running that will give you a message with question marks:

	   $ LANGUAGE= LANG=de_DE.utf8 ./test
	   test: Kein passendes Ger?t gefunden

	   The vsnprintf bug has been fixed since glibc 2.17.

	   Then we could simply set LC_CTYPE from the environment, which would
	   make things like the external perror(3) messages work.

	   See t/t0203-gettext-setlocale-sanity.sh's "gettext.c" tests for
	   regression tests.

	   1. http://sourceware.org/bugzilla/show_bug.cgi?id=6530
	   2. E.g. "Content-Type: text/plain; charset=UTF-8\n" in po/is.po
	*/
	setlocale(LC_CTYPE, "");
	charset = locale_charset();
	bind_textdomain_codeset(domain, charset);
	/* the string is taken from v0.99.6~1 */
	if (test_vsnprintf("%.*s", 13, "David_K\345gedal") < 0)
		setlocale(LC_CTYPE, "C");
}

void git_setup_gettext(void)
{
	const char *podir = getenv("GIT_TEXTDOMAINDIR");
	char *p = NULL;

	if (!podir)
		podir = GIT_LOCALE_PATH;
	if (!is_absolute_path(podir))
		podir = p = system_path(podir);

	if (is_directory(podir)) {
		bindtextdomain("git", podir);
		setlocale(LC_MESSAGES, "");
		setlocale(LC_TIME, "");
		init_gettext_charset("git");
		textdomain("git");
	}

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
