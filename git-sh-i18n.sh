#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#
# This is Git's interface to gettext.sh. Use it right after
# git-sh-setup as:
#
#   . git-sh-setup
#   . git-sh-i18n
#
#   # For constant interface messages:
#   gettext "A message for the user"; echo
#
#   # To interpolate variables:
#   details="oh noes"
#   eval_gettext "An error occured: \$details"; echo
#
# See "info '(gettext)sh'" for the full manual.

# Export the TEXTDOMAIN* data that we need for Git
TEXTDOMAIN=git
export TEXTDOMAIN
if [ -z "$GIT_TEXTDOMAINDIR" ]
then
	TEXTDOMAINDIR="@@LOCALEDIR@@"
else
	TEXTDOMAINDIR="$GIT_TEXTDOMAINDIR"
fi
export TEXTDOMAINDIR

if test -z "$GIT_INTERNAL_GETTEXT_TEST_FALLBACKS" && type gettext.sh >/dev/null 2>&1
then
	# This is GNU libintl's gettext.sh, we don't need to do anything
	# else than setting up the environment and loading gettext.sh
	GIT_INTERNAL_GETTEXT_SH_SCHEME=gnu
	export GIT_INTERNAL_GETTEXT_SH_SCHEME

	# Try to use libintl's gettext.sh, or fall back to English if we
	# can't.
	. gettext.sh
elif test -z "$GIT_INTERNAL_GETTEXT_TEST_FALLBACKS" && test "$(gettext -h 2>&1)" = "-h"
then
	# We don't have gettext.sh, but there's a gettext binary in our
	# path. This is probably Solaris or something like it which has a
	# gettext implementation that isn't GNU libintl.
	GIT_INTERNAL_GETTEXT_SH_SCHEME=solaris
	export GIT_INTERNAL_GETTEXT_SH_SCHEME

	# Solaris has a gettext(1) but no eval_gettext(1)
	eval_gettext () {
		gettext_out=$(gettext "$1")
		gettext_eval="printf '%s' \"$gettext_out\""
		printf "%s" "`eval \"$gettext_eval\"`"
	}
else
	# Since gettext.sh isn't available we'll have to define our own
	# dummy pass-through functions.

	# Tell our tests that we don't have the real gettext.sh
	GIT_INTERNAL_GETTEXT_SH_SCHEME=fallthrough
	export GIT_INTERNAL_GETTEXT_SH_SCHEME

	gettext () {
		printf "%s" "$1"
	}

	eval_gettext () {
		gettext_eval="printf '%s' \"$1\""
		printf "%s" "`eval \"$gettext_eval\"`"
	}
fi
