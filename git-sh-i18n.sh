# This shell library is Git's interface to gettext.sh. See po/README
# for usage instructions.
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

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

# First decide what scheme to use...
GIT_INTERNAL_GETTEXT_SH_SCHEME=fallthrough
if test -n "@@USE_GETTEXT_SCHEME@@"
then
	GIT_INTERNAL_GETTEXT_SH_SCHEME="@@USE_GETTEXT_SCHEME@@"
elif test -n "$GIT_INTERNAL_GETTEXT_TEST_FALLBACKS"
then
	: no probing necessary
elif test -n "$GIT_GETTEXT_POISON"
then
	GIT_INTERNAL_GETTEXT_SH_SCHEME=poison
elif type gettext.sh >/dev/null 2>&1
then
	# GNU libintl's gettext.sh
	GIT_INTERNAL_GETTEXT_SH_SCHEME=gnu
elif test "$(gettext -h 2>&1)" = "-h"
then
	# gettext binary exists but no gettext.sh. likely to be a gettext
	# binary on a Solaris or something that is not GNU libintl and
	# lack eval_gettext.
	GIT_INTERNAL_GETTEXT_SH_SCHEME=gettext_without_eval_gettext
fi
export GIT_INTERNAL_GETTEXT_SH_SCHEME

# ... and then follow that decision.
case "$GIT_INTERNAL_GETTEXT_SH_SCHEME" in
gnu)
	# Use libintl's gettext.sh, or fall back to English if we can't.
	. gettext.sh
	;;
gettext_without_eval_gettext)
	# Solaris has a gettext(1) but no eval_gettext(1)
	eval_gettext () {
		gettext "$1" | (
			export PATH $(git sh-i18n--envsubst --variables "$1");
			git sh-i18n--envsubst "$1"
		)
	}
	;;
poison)
	# Emit garbage so that tests that incorrectly rely on translatable
	# strings will fail.
	gettext () {
		printf "%s" "# GETTEXT POISON #"
	}

	eval_gettext () {
		printf "%s" "# GETTEXT POISON #"
	}
	;;
*)
	gettext () {
		printf "%s" "$1"
	}

	eval_gettext () {
		printf "%s" "$1" | (
			export PATH $(git sh-i18n--envsubst --variables "$1");
			git sh-i18n--envsubst "$1"
		)
	}
	;;
esac

# Git-specific wrapper functions
gettextln () {
	gettext "$1"
	echo
}

eval_gettextln () {
	eval_gettext "$1"
	echo
}
