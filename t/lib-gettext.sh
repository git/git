#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

. ./test-lib.sh

GIT_TEXTDOMAINDIR="$GIT_EXEC_PATH/share/locale"
GIT_PO_PATH="$GIT_EXEC_PATH/po"
export GIT_TEXTDOMAINDIR GIT_PO_PATH

. "$GIT_EXEC_PATH"/git-sh-i18n

if test_have_prereq GETTEXT
then
	# is_IS.UTF-8 on Solaris and FreeBSD, is_IS.utf8 on Debian
	is_IS_locale=$(locale -a | sed -n '/^is_IS\.[uU][tT][fF]-*8$/{
		p
		q
	}')
	# Export it as an environmental variable so the t0202/test.pl Perl
	# test can use it too
	export is_IS_locale

	if test -n "$is_IS_locale" &&
		test $GIT_INTERNAL_GETTEXT_SH_SCHEME != "fallthrough"
	then
		# Some of the tests need the reference Icelandic locale
		test_set_prereq GETTEXT_LOCALE

		# Exporting for t0202/test.pl
		GETTEXT_LOCALE=1
		export GETTEXT_LOCALE
		say "# lib-gettext: Found '$is_IS_locale' as a is_IS UTF-8 locale"
	else
		say "# lib-gettext: No is_IS UTF-8 locale available"
	fi
else
	# Only run some tests when we don't have gettext support
	test_set_prereq NO_GETTEXT
	say "# lib-gettext: No GETTEXT support available"
fi
