#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#
# This is a skeleton no-op implementation of gettext for Git. It'll be
# replaced by something that uses gettext.sh in a future patch series.

if test -z "$GIT_GETTEXT_POISON"
then
	gettext () {
		printf "%s" "$1"
	}

	gettextln() {
		printf "%s\n" "$1"
	}

	eval_gettext () {
		printf "%s" "$1" | (
			export PATH $(git sh-i18n--envsubst --variables "$1");
			git sh-i18n--envsubst "$1"
		)
	}

	eval_gettextln () {
		printf "%s\n" "$1" | (
			export PATH $(git sh-i18n--envsubst --variables "$1");
			git sh-i18n--envsubst "$1"
		)
	}
else
	gettext () {
		printf "%s" "# GETTEXT POISON #"
	}

	gettextln () {
		printf "%s\n" "# GETTEXT POISON #"
	}

	eval_gettext () {
		printf "%s" "# GETTEXT POISON #"
	}

	eval_gettextln () {
		printf "%s\n" "# GETTEXT POISON #"
	}
fi

