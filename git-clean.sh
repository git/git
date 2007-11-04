#!/bin/sh
#
# Copyright (c) 2005-2006 Pavel Roskin
#

OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC="\
git-clean [options] <paths>...

Clean untracked files from the working directory

When optional <paths>... arguments are given, the paths
affected are further limited to those that match them.
--
d remove directories as well
f override clean.requireForce and clean anyway
n don't remove anything, just show what would be done
q be quiet, only report errors
x remove ignored files as well
X remove only ignored files"

SUBDIRECTORY_OK=Yes
. git-sh-setup
require_work_tree

ignored=
ignoredonly=
cleandir=
disabled="`git config --bool clean.requireForce`"
rmf="rm -f --"
rmrf="rm -rf --"
rm_refuse="echo Not removing"
echo1="echo"

while test $# != 0
do
	case "$1" in
	-d)
		cleandir=1
		;;
	-f)
		disabled=
		;;
	-n)
		disabled=
		rmf="echo Would remove"
		rmrf="echo Would remove"
		rm_refuse="echo Would not remove"
		echo1=":"
		;;
	-q)
		echo1=":"
		;;
	-x)
		ignored=1
		;;
	-X)
		ignoredonly=1
		;;
	--)
		shift
		break
		;;
	*)
		usage # should not happen
		;;
	esac
	shift
done

if [ "$disabled" = true ]; then
	die "clean.requireForce set and -n or -f not given; refusing to clean"
fi

if [ "$ignored,$ignoredonly" = "1,1" ]; then
	die "-x and -X cannot be set together"
fi

if [ -z "$ignored" ]; then
	excl="--exclude-per-directory=.gitignore"
	if [ -f "$GIT_DIR/info/exclude" ]; then
		excl_info="--exclude-from=$GIT_DIR/info/exclude"
	fi
	if [ "$ignoredonly" ]; then
		excl="$excl --ignored"
	fi
fi

git ls-files --others --directory $excl ${excl_info:+"$excl_info"} -- "$@" |
while read -r file; do
	if [ -d "$file" -a ! -L "$file" ]; then
		if [ -z "$cleandir" ]; then
			$rm_refuse "$file"
			continue
		fi
		$echo1 "Removing $file"
		$rmrf "$file"
	else
		$echo1 "Removing $file"
		$rmf "$file"
	fi
done
