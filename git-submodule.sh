#!/bin/sh
#
# git-submodules.sh: init, update or list git submodules
#
# Copyright (c) 2007 Lars Hjemli

USAGE='[--quiet] [--cached] [status|init|update] [--] [<path>...]'
. git-sh-setup
require_work_tree

init=
update=
status=
quiet=
cached=

#
# print stuff on stdout unless -q was specified
#
say()
{
	if test -z "$quiet"
	then
		echo "$@"
	fi
}


#
# Clone a submodule
#
module_clone()
{
	path=$1
	url=$2

	# If there already is a directory at the submodule path,
	# expect it to be empty (since that is the default checkout
	# action) and try to remove it.
	# Note: if $path is a symlink to a directory the test will
	# succeed but the rmdir will fail. We might want to fix this.
	if test -d "$path"
	then
		rmdir "$path" 2>/dev/null ||
		die "Directory '$path' exist, but is neither empty nor a git repository"
	fi

	test -e "$path" &&
	die "A file already exist at path '$path'"

	git-clone -n "$url" "$path" ||
	die "Clone of submodule '$path' failed"
}

#
# Run clone + checkout on missing submodules
#
# $@ = requested paths (default to all)
#
modules_init()
{
	git ls-files --stage -- "$@" | grep -e '^160000 ' |
	while read mode sha1 stage path
	do
		# Skip submodule paths that already contain a .git directory.
		# This will also trigger if $path is a symlink to a git
		# repository
		test -d "$path"/.git && continue

		url=$(GIT_CONFIG=.gitmodules git-config module."$path".url)
		test -z "$url" &&
		die "No url found for submodule '$path' in .gitmodules"

		# MAYBE FIXME: this would be the place to check GIT_CONFIG
		# for a preferred url for this submodule, possibly like this:
		#
		# modname=$(GIT_CONFIG=.gitmodules git-config module."$path".name)
		# alturl=$(git-config module."$modname".url)
		#
		# This would let the versioned .gitmodules file use the submodule
		# path as key, while the unversioned GIT_CONFIG would use the
		# logical modulename (if present) as key. But this would need
		# another fallback mechanism if the module wasn't named.

		module_clone "$path" "$url" || exit

		(unset GIT_DIR && cd "$path" && git-checkout -q "$sha1") ||
		die "Checkout of submodule '$path' failed"

		say "Submodule '$path' initialized"
	done
}

#
# Checkout correct revision of each initialized submodule
#
# $@ = requested paths (default to all)
#
modules_update()
{
	git ls-files --stage -- "$@" | grep -e '^160000 ' |
	while read mode sha1 stage path
	do
		if ! test -d "$path"/.git
		then
			# Only mention uninitialized submodules when its
			# path have been specified
			test "$#" != "0" &&
			say "Submodule '$path' not initialized"
			continue;
		fi
		subsha1=$(unset GIT_DIR && cd "$path" &&
			git-rev-parse --verify HEAD) ||
		die "Unable to find current revision of submodule '$path'"

		if test "$subsha1" != "$sha1"
		then
			(unset GIT_DIR && cd "$path" && git-fetch &&
				git-checkout -q "$sha1") ||
			die "Unable to checkout '$sha1' in submodule '$path'"

			say "Submodule '$path': checked out '$sha1'"
		fi
	done
}

#
# List all registered submodules, prefixed with:
#  - submodule not initialized
#  + different revision checked out
#
# If --cached was specified the revision in the index will be printed
# instead of the currently checked out revision.
#
# $@ = requested paths (default to all)
#
modules_list()
{
	git ls-files --stage -- "$@" | grep -e '^160000 ' |
	while read mode sha1 stage path
	do
		if ! test -d "$path"/.git
		then
			say "-$sha1 $path"
			continue;
		fi
		revname=$(unset GIT_DIR && cd "$path" && git-describe $sha1)
		if git diff-files --quiet -- "$path"
		then
			say " $sha1 $path ($revname)"
		else
			if test -z "$cached"
			then
				sha1=$(unset GIT_DIR && cd "$path" && git-rev-parse --verify HEAD)
				revname=$(unset GIT_DIR && cd "$path" && git-describe $sha1)
			fi
			say "+$sha1 $path ($revname)"
		fi
	done
}

while case "$#" in 0) break ;; esac
do
	case "$1" in
	init)
		init=1
		;;
	update)
		update=1
		;;
	status)
		status=1
		;;
	-q|--quiet)
		quiet=1
		;;
	--cached)
		cached=1
		;;
	--)
		break
		;;
	-*)
		usage
		;;
	*)
		break
		;;
	esac
	shift
done

case "$init,$update,$status,$cached" in
1,,,)
	modules_init "$@"
	;;
,1,,)
	modules_update "$@"
	;;
,,*,*)
	modules_list "$@"
	;;
*)
	usage
	;;
esac
