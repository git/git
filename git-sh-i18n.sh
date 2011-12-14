#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#
# This is Git's interface to gettext.sh. See po/README for usage
# instructions.

# Export the TEXTDOMAIN* data that we need for Git
TEXTDOMAIN=git
export TEXTDOMAIN
if test -z "$GIT_TEXTDOMAINDIR"
then
	TEXTDOMAINDIR="@@LOCALEDIR@@"
else
	TEXTDOMAINDIR="$GIT_TEXTDOMAINDIR"
fi
export TEXTDOMAINDIR

if test -z "$GIT_GETTEXT_POISON"
then
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
			gettext "$1" | (
				export PATH $(git sh-i18n--envsubst --variables "$1");
				git sh-i18n--envsubst "$1"
			)
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
			printf "%s" "$1" | (
				export PATH $(git sh-i18n--envsubst --variables "$1");
				git sh-i18n--envsubst "$1"
			)
		}
	fi
else
	# Emit garbage under GETTEXT_POISON=YesPlease. Unlike the C tests
	# this relies on an environment variable

	GIT_INTERNAL_GETTEXT_SH_SCHEME=poison
	export GIT_INTERNAL_GETTEXT_SH_SCHEME

	gettext () {
		printf "%s" "# GETTEXT POISON #"
	}

	eval_gettext () {
		printf "%s" "# GETTEXT POISON #"
	}
fi

# Git-specific wrapper functions
gettextln () {
	gettext "$1"
	echo
}

eval_gettextln () {
	eval_gettext "$1"
	echo
}
