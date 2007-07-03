#!/bin/sh
#
# git-submodules.sh: add, init, update or list git submodules
#
# Copyright (c) 2007 Lars Hjemli

USAGE='[--quiet] [--cached] [add <repo> [-b branch]|status|init|update] [--] [<path>...]'
. git-sh-setup
require_work_tree

add=
branch=
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

# NEEDSWORK: identical function exists in get_repo_base in clone.sh
get_repo_base() {
	(
		cd "`/bin/pwd`" &&
		cd "$1" || cd "$1.git" &&
		{
			cd .git
			pwd
		}
	) 2>/dev/null
}

#
# Map submodule path to submodule name
#
# $1 = path
#
module_name()
{
       name=$(GIT_CONFIG=.gitmodules git config --get-regexp '^submodule\..*\.path$' "$1" |
       sed -nre 's/^submodule\.(.+)\.path .+$/\1/p')
       test -z "$name" &&
       die "No submodule mapping found in .gitmodules for path '$path'"
       echo "$name"
}

#
# Clone a submodule
#
# Prior to calling, modules_update checks that a possibly existing
# path is not a git repository.
# Likewise, module_add checks that path does not exist at all,
# since it is the location of a new submodule.
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
	die "Clone of '$url' into submodule path '$path' failed"
}

#
# Add a new submodule to the working tree, .gitmodules and the index
#
# $@ = repo [path]
#
# optional branch is stored in global branch variable
#
module_add()
{
	repo=$1
	path=$2

	if test -z "$repo"; then
		usage
	fi

	# Turn the source into an absolute path if
	# it is local
	if base=$(get_repo_base "$repo"); then
		repo="$base"
	fi

	# Guess path from repo if not specified or strip trailing slashes
	if test -z "$path"; then
		path=$(echo "$repo" | sed -e 's|/*$||' -e 's|:*/*\.git$||' -e 's|.*[/:]||g')
	else
		path=$(echo "$path" | sed -e 's|/*$||')
	fi

	test -e "$path" &&
	die "'$path' already exists"

	git ls-files --error-unmatch "$path" > /dev/null 2>&1 &&
	die "'$path' already exists in the index"

	module_clone "$path" "$repo" || exit
	(unset GIT_DIR && cd "$path" && git checkout -q ${branch:+-b "$branch" "origin/$branch"}) ||
	die "Unable to checkout submodule '$path'"
	git add "$path" ||
	die "Failed to add submodule '$path'"

	GIT_CONFIG=.gitmodules git config submodule."$path".path "$path" &&
	GIT_CONFIG=.gitmodules git config submodule."$path".url "$repo" &&
	git add .gitmodules ||
	die "Failed to register submodule '$path'"
}

#
# Register submodules in .git/config
#
# $@ = requested paths (default to all)
#
modules_init()
{
	git ls-files --stage -- "$@" | grep -e '^160000 ' |
	while read mode sha1 stage path
	do
		# Skip already registered paths
		name=$(module_name "$path") || exit
		url=$(git config submodule."$name".url)
		test -z "$url" || continue

		url=$(GIT_CONFIG=.gitmodules git config submodule."$name".url)
		test -z "$url" &&
		die "No url found for submodule path '$path' in .gitmodules"

		git config submodule."$name".url "$url" ||
		die "Failed to register url for submodule path '$path'"

		say "Submodule '$name' ($url) registered for path '$path'"
	done
}

#
# Update each submodule path to correct revision, using clone and checkout as needed
#
# $@ = requested paths (default to all)
#
modules_update()
{
	git ls-files --stage -- "$@" | grep -e '^160000 ' |
	while read mode sha1 stage path
	do
		name=$(module_name "$path") || exit
		url=$(git config submodule."$name".url)
		if test -z "$url"
		then
			# Only mention uninitialized submodules when its
			# path have been specified
			test "$#" != "0" &&
			say "Submodule path '$path' not initialized"
			continue
		fi

		if ! test -d "$path"/.git
		then
			module_clone "$path" "$url" || exit
			subsha1=
		else
			subsha1=$(unset GIT_DIR && cd "$path" &&
				git rev-parse --verify HEAD) ||
			die "Unable to find current revision in submodule path '$path'"
		fi

		if test "$subsha1" != "$sha1"
		then
			(unset GIT_DIR && cd "$path" && git-fetch &&
				git-checkout -q "$sha1") ||
			die "Unable to checkout '$sha1' in submodule path '$path'"

			say "Submodule path '$path': checked out '$sha1'"
		fi
	done
}

set_name_rev () {
	revname=$( (
		unset GIT_DIR &&
		cd "$1" && {
			git describe "$2" 2>/dev/null ||
			git describe --tags "$2" 2>/dev/null ||
			git describe --contains --tags "$2"
		}
	) )
	test -z "$revname" || revname=" ($revname)"
}

#
# List all submodules, prefixed with:
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
		name=$(module_name "$path") || exit
		url=$(git config submodule."$name".url)
		if test -z "url" || ! test -d "$path"/.git
		then
			say "-$sha1 $path"
			continue;
		fi
		revname=$(unset GIT_DIR && cd "$path" && git describe --tags $sha1)
		set_name_rev "$path" $"sha1"
		if git diff-files --quiet -- "$path"
		then
			say " $sha1 $path$revname"
		else
			if test -z "$cached"
			then
				sha1=$(unset GIT_DIR && cd "$path" && git rev-parse --verify HEAD)
				set_name_rev "$path" $"sha1"
			fi
			say "+$sha1 $path$revname"
		fi
	done
}

while case "$#" in 0) break ;; esac
do
	case "$1" in
	add)
		add=1
		;;
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
	-b|--branch)
		case "$2" in
		'')
			usage
			;;
		esac
		branch="$2"; shift
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

case "$add,$branch" in
1,*)
	;;
,)
	;;
,*)
	usage
	;;
esac

case "$add,$init,$update,$status,$cached" in
1,,,,)
	module_add "$@"
	;;
,1,,,)
	modules_init "$@"
	;;
,,1,,)
	modules_update "$@"
	;;
,,,1,*)
	modules_list "$@"
	;;
*)
	usage
	;;
esac
