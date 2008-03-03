#!/bin/sh
#
# git-submodules.sh: add, init, update or list git submodules
#
# Copyright (c) 2007 Lars Hjemli

USAGE='[--quiet] [--cached] [add <repo> [-b branch]|status|init|update] [--] [<path>...]'
OPTIONS_SPEC=
. git-sh-setup
require_work_tree

command=
branch=
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

# Resolve relative url by appending to parent's url
resolve_relative_url ()
{
	branch="$(git symbolic-ref HEAD 2>/dev/null)"
	remote="$(git config branch.${branch#refs/heads/}.remote)"
	remote="${remote:-origin}"
	remoteurl="$(git config remote.$remote.url)" ||
		die "remote ($remote) does not have a url in .git/config"
	url="$1"
	while test -n "$url"
	do
		case "$url" in
		../*)
			url="${url#../}"
			remoteurl="${remoteurl%/*}"
			;;
		./*)
			url="${url#./}"
			;;
		*)
			break;;
		esac
	done
	echo "$remoteurl/$url"
}

#
# Map submodule path to submodule name
#
# $1 = path
#
module_name()
{
	# Do we have "submodule.<something>.path = $1" defined in .gitmodules file?
	re=$(printf '%s' "$1" | sed -e 's/[].[^$\\*]/\\&/g')
	name=$( GIT_CONFIG=.gitmodules \
		git config --get-regexp '^submodule\..*\.path$' |
		sed -n -e 's|^submodule\.\(.*\)\.path '"$re"'$|\1|p' )
       test -z "$name" &&
       die "No submodule mapping found in .gitmodules for path '$path'"
       echo "$name"
}

#
# Clone a submodule
#
# Prior to calling, cmd_update checks that a possibly existing
# path is not a git repository.
# Likewise, cmd_add checks that path does not exist at all,
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
cmd_add()
{
	# parse $args after "submodule ... add".
	while test $# -ne 0
	do
		case "$1" in
		-b | --branch)
			case "$2" in '') usage ;; esac
			branch=$2
			shift
			;;
		-q|--quiet)
			quiet=1
			;;
		--)
			shift
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

	repo=$1
	path=$2

	if test -z "$repo"; then
		usage
	fi

	case "$repo" in
	./*|../*)
		# dereference source url relative to parent's url
		realrepo="$(resolve_relative_url $repo)" ;;
	*)
		# Turn the source into an absolute path if
		# it is local
		if base=$(get_repo_base "$repo"); then
			repo="$base"
		fi
		realrepo=$repo
		;;
	esac

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

	module_clone "$path" "$realrepo" || exit
	(unset GIT_DIR; cd "$path" && git checkout -q ${branch:+-b "$branch" "origin/$branch"}) ||
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
cmd_init()
{
	# parse $args after "submodule ... init".
	while test $# -ne 0
	do
		case "$1" in
		-q|--quiet)
			quiet=1
			;;
		--)
			shift
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

		# Possibly a url relative to parent
		case "$url" in
		./*|../*)
			url="$(resolve_relative_url "$url")"
			;;
		esac

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
cmd_update()
{
	# parse $args after "submodule ... update".
	while test $# -ne 0
	do
		case "$1" in
		-q|--quiet)
			quiet=1
			;;
		--)
			shift
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
			subsha1=$(unset GIT_DIR; cd "$path" &&
				git rev-parse --verify HEAD) ||
			die "Unable to find current revision in submodule path '$path'"
		fi

		if test "$subsha1" != "$sha1"
		then
			(unset GIT_DIR; cd "$path" && git-fetch &&
				git-checkout -q "$sha1") ||
			die "Unable to checkout '$sha1' in submodule path '$path'"

			say "Submodule path '$path': checked out '$sha1'"
		fi
	done
}

set_name_rev () {
	revname=$( (
		unset GIT_DIR
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
cmd_status()
{
	# parse $args after "submodule ... status".
	while test $# -ne 0
	do
		case "$1" in
		-q|--quiet)
			quiet=1
			;;
		--cached)
			cached=1
			;;
		--)
			shift
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

	git ls-files --stage -- "$@" | grep -e '^160000 ' |
	while read mode sha1 stage path
	do
		name=$(module_name "$path") || exit
		url=$(git config submodule."$name".url)
		if test -z "$url" || ! test -d "$path"/.git
		then
			say "-$sha1 $path"
			continue;
		fi
		set_name_rev "$path" "$sha1"
		if git diff-files --quiet -- "$path"
		then
			say " $sha1 $path$revname"
		else
			if test -z "$cached"
			then
				sha1=$(unset GIT_DIR; cd "$path" && git rev-parse --verify HEAD)
				set_name_rev "$path" "$sha1"
			fi
			say "+$sha1 $path$revname"
		fi
	done
}

# This loop parses the command line arguments to find the
# subcommand name to dispatch.  Parsing of the subcommand specific
# options are primarily done by the subcommand implementations.
# Subcommand specific options such as --branch and --cached are
# parsed here as well, for backward compatibility.

while test $# != 0 && test -z "$command"
do
	case "$1" in
	add | init | update | status)
		command=$1
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

# No command word defaults to "status"
test -n "$command" || command=status

# "-b branch" is accepted only by "add"
if test -n "$branch" && test "$command" != add
then
	usage
fi

# "--cached" is accepted only by "status"
if test -n "$cached" && test "$command" != status
then
	usage
fi

"cmd_$command" "$@"
