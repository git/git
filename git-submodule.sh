#!/bin/sh
#
# git-submodules.sh: add, init, update or list git submodules
#
# Copyright (c) 2007 Lars Hjemli

USAGE="[--quiet] [--cached] \
[add [-b branch] <repo> <path>]|[status|init|update [-i|--init] [-N|--no-fetch]|summary [-n|--summary-limit <n>] [<commit>]] \
[--] [<path>...]|[foreach <command>]|[sync [--] [<path>...]]"
OPTIONS_SPEC=
. git-sh-setup
. git-parse-remote
require_work_tree

command=
branch=
quiet=
cached=
nofetch=

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

# Resolve relative url by appending to parent's url
resolve_relative_url ()
{
	remote=$(get_default_remote)
	remoteurl=$(git config "remote.$remote.url") ||
		die "remote ($remote) does not have a url defined in .git/config"
	url="$1"
	remoteurl=${remoteurl%/}
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
	echo "$remoteurl/${url%/}"
}

#
# Get submodule info for registered submodules
# $@ = path to limit submodule list
#
module_list()
{
	git ls-files --error-unmatch --stage -- "$@" | grep '^160000 '
}

#
# Map submodule path to submodule name
#
# $1 = path
#
module_name()
{
	# Do we have "submodule.<something>.path = $1" defined in .gitmodules file?
	re=$(printf '%s\n' "$1" | sed -e 's/[].[^$\\*]/\\&/g')
	name=$( git config -f .gitmodules --get-regexp '^submodule\..*\.path$' |
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
# $@ = repo path
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

	if test -z "$repo" -o -z "$path"; then
		usage
	fi

	# assure repo is absolute or relative to parent
	case "$repo" in
	./*|../*)
		# dereference source url relative to parent's url
		realrepo=$(resolve_relative_url "$repo") || exit
		;;
	*:*|/*)
		# absolute url
		realrepo=$repo
		;;
	*)
		die "repo URL: '$repo' must be absolute or begin with ./|../"
	;;
	esac

	# normalize path:
	# multiple //; leading ./; /./; /../; trailing /
	path=$(printf '%s/\n' "$path" |
		sed -e '
			s|//*|/|g
			s|^\(\./\)*||
			s|/\./|/|g
			:start
			s|\([^/]*\)/\.\./||
			tstart
			s|/*$||
		')
	git ls-files --error-unmatch "$path" > /dev/null 2>&1 &&
	die "'$path' already exists in the index"

	# perhaps the path exists and is already a git repo, else clone it
	if test -e "$path"
	then
		if test -d "$path"/.git -o -f "$path"/.git
		then
			echo "Adding existing repo at '$path' to the index"
		else
			die "'$path' already exists and is not a valid git repo"
		fi

		case "$repo" in
		./*|../*)
			url=$(resolve_relative_url "$repo") || exit
		    ;;
		*)
			url="$repo"
			;;
		esac
		git config submodule."$path".url "$url"
	else

		module_clone "$path" "$realrepo" || exit
		(
			unset GIT_DIR
			cd "$path" &&
			# ash fails to wordsplit ${branch:+-b "$branch"...}
			case "$branch" in
			'') git checkout -f -q ;;
			?*) git checkout -f -q -b "$branch" "origin/$branch" ;;
			esac
		) || die "Unable to checkout submodule '$path'"
	fi

	git add "$path" ||
	die "Failed to add submodule '$path'"

	git config -f .gitmodules submodule."$path".path "$path" &&
	git config -f .gitmodules submodule."$path".url "$repo" &&
	git add .gitmodules ||
	die "Failed to register submodule '$path'"
}

#
# Execute an arbitrary command sequence in each checked out
# submodule
#
# $@ = command to execute
#
cmd_foreach()
{
	module_list |
	while read mode sha1 stage path
	do
		if test -e "$path"/.git
		then
			say "Entering '$path'"
			(cd "$path" && eval "$@") ||
			die "Stopping at '$path'; script returned non-zero status."
		fi
	done
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

	module_list "$@" |
	while read mode sha1 stage path
	do
		# Skip already registered paths
		name=$(module_name "$path") || exit
		url=$(git config submodule."$name".url)
		test -z "$url" || continue

		url=$(git config -f .gitmodules submodule."$name".url)
		test -z "$url" &&
		die "No url found for submodule path '$path' in .gitmodules"

		# Possibly a url relative to parent
		case "$url" in
		./*|../*)
			url=$(resolve_relative_url "$url") || exit
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
			shift
			quiet=1
			;;
		-i|--init)
			shift
			cmd_init "$@" || return
			;;
		-N|--no-fetch)
			shift
			nofetch=1
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
	done

	module_list "$@" |
	while read mode sha1 stage path
	do
		name=$(module_name "$path") || exit
		url=$(git config submodule."$name".url)
		if test -z "$url"
		then
			# Only mention uninitialized submodules when its
			# path have been specified
			test "$#" != "0" &&
			say "Submodule path '$path' not initialized" &&
			say "Maybe you want to use 'update --init'?"
			continue
		fi

		if ! test -d "$path"/.git -o -f "$path"/.git
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
			force=
			if test -z "$subsha1"
			then
				force="-f"
			fi

			if test -z "$nofetch"
			then
				(unset GIT_DIR; cd "$path" &&
					git-fetch) ||
				die "Unable to fetch in submodule path '$path'"
			fi

			(unset GIT_DIR; cd "$path" &&
				  git-checkout $force -q "$sha1") ||
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
			git describe --contains "$2" 2>/dev/null ||
			git describe --all --always "$2"
		}
	) )
	test -z "$revname" || revname=" ($revname)"
}
#
# Show commit summary for submodules in index or working tree
#
# If '--cached' is given, show summary between index and given commit,
# or between working tree and given commit
#
# $@ = [commit (default 'HEAD'),] requested paths (default all)
#
cmd_summary() {
	summary_limit=-1
	for_status=

	# parse $args after "submodule ... summary".
	while test $# -ne 0
	do
		case "$1" in
		--cached)
			cached="$1"
			;;
		--for-status)
			for_status="$1"
			;;
		-n|--summary-limit)
			if summary_limit=$(($2 + 0)) 2>/dev/null && test "$summary_limit" = "$2"
			then
				:
			else
				usage
			fi
			shift
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

	test $summary_limit = 0 && return

	if rev=$(git rev-parse -q --verify "$1^0")
	then
		head=$rev
		shift
	else
		head=HEAD
	fi

	cd_to_toplevel
	# Get modified modules cared by user
	modules=$(git diff-index $cached --raw $head -- "$@" |
		egrep '^:([0-7]* )?160000' |
		while read mod_src mod_dst sha1_src sha1_dst status name
		do
			# Always show modules deleted or type-changed (blob<->module)
			test $status = D -o $status = T && echo "$name" && continue
			# Also show added or modified modules which are checked out
			GIT_DIR="$name/.git" git-rev-parse --git-dir >/dev/null 2>&1 &&
			echo "$name"
		done
	)

	test -z "$modules" && return

	git diff-index $cached --raw $head -- $modules |
	egrep '^:([0-7]* )?160000' |
	cut -c2- |
	while read mod_src mod_dst sha1_src sha1_dst status name
	do
		if test -z "$cached" &&
			test $sha1_dst = 0000000000000000000000000000000000000000
		then
			case "$mod_dst" in
			160000)
				sha1_dst=$(GIT_DIR="$name/.git" git rev-parse HEAD)
				;;
			100644 | 100755 | 120000)
				sha1_dst=$(git hash-object $name)
				;;
			000000)
				;; # removed
			*)
				# unexpected type
				echo >&2 "unexpected mode $mod_dst"
				continue ;;
			esac
		fi
		missing_src=
		missing_dst=

		test $mod_src = 160000 &&
		! GIT_DIR="$name/.git" git-rev-parse -q --verify $sha1_src^0 >/dev/null &&
		missing_src=t

		test $mod_dst = 160000 &&
		! GIT_DIR="$name/.git" git-rev-parse -q --verify $sha1_dst^0 >/dev/null &&
		missing_dst=t

		total_commits=
		case "$missing_src,$missing_dst" in
		t,)
			errmsg="  Warn: $name doesn't contain commit $sha1_src"
			;;
		,t)
			errmsg="  Warn: $name doesn't contain commit $sha1_dst"
			;;
		t,t)
			errmsg="  Warn: $name doesn't contain commits $sha1_src and $sha1_dst"
			;;
		*)
			errmsg=
			total_commits=$(
			if test $mod_src = 160000 -a $mod_dst = 160000
			then
				range="$sha1_src...$sha1_dst"
			elif test $mod_src = 160000
			then
				range=$sha1_src
			else
				range=$sha1_dst
			fi
			GIT_DIR="$name/.git" \
			git log --pretty=oneline --first-parent $range | wc -l
			)
			total_commits=" ($(($total_commits + 0)))"
			;;
		esac

		sha1_abbr_src=$(echo $sha1_src | cut -c1-7)
		sha1_abbr_dst=$(echo $sha1_dst | cut -c1-7)
		if test $status = T
		then
			if test $mod_dst = 160000
			then
				echo "* $name $sha1_abbr_src(blob)->$sha1_abbr_dst(submodule)$total_commits:"
			else
				echo "* $name $sha1_abbr_src(submodule)->$sha1_abbr_dst(blob)$total_commits:"
			fi
		else
			echo "* $name $sha1_abbr_src...$sha1_abbr_dst$total_commits:"
		fi
		if test -n "$errmsg"
		then
			# Don't give error msg for modification whose dst is not submodule
			# i.e. deleted or changed to blob
			test $mod_dst = 160000 && echo "$errmsg"
		else
			if test $mod_src = 160000 -a $mod_dst = 160000
			then
				limit=
				test $summary_limit -gt 0 && limit="-$summary_limit"
				GIT_DIR="$name/.git" \
				git log $limit --pretty='format:  %m %s' \
				--first-parent $sha1_src...$sha1_dst
			elif test $mod_dst = 160000
			then
				GIT_DIR="$name/.git" \
				git log --pretty='format:  > %s' -1 $sha1_dst
			else
				GIT_DIR="$name/.git" \
				git log --pretty='format:  < %s' -1 $sha1_src
			fi
			echo
		fi
		echo
	done |
	if test -n "$for_status"; then
		echo "# Modified submodules:"
		echo "#"
		sed -e 's|^|# |' -e 's|^# $|#|'
	else
		cat
	fi
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

	module_list "$@" |
	while read mode sha1 stage path
	do
		name=$(module_name "$path") || exit
		url=$(git config submodule."$name".url)
		if test -z "$url" || ! test -d "$path"/.git -o -f "$path"/.git
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
#
# Sync remote urls for submodules
# This makes the value for remote.$remote.url match the value
# specified in .gitmodules.
#
cmd_sync()
{
	while test $# -ne 0
	do
		case "$1" in
		-q|--quiet)
			quiet=1
			shift
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
	done
	cd_to_toplevel
	module_list "$@" |
	while read mode sha1 stage path
	do
		name=$(module_name "$path")
		url=$(git config -f .gitmodules --get submodule."$name".url)

		# Possibly a url relative to parent
		case "$url" in
		./*|../*)
			url=$(resolve_relative_url "$url") || exit
			;;
		esac

		if test -e "$path"/.git
		then
		(
			unset GIT_DIR
			cd "$path"
			remote=$(get_default_remote)
			say "Synchronizing submodule url for '$name'"
			git config remote."$remote".url "$url"
		)
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
	add | foreach | init | update | status | summary | sync)
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
		cached="$1"
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

# "--cached" is accepted only by "status" and "summary"
if test -n "$cached" && test "$command" != status -a "$command" != summary
then
	usage
fi

"cmd_$command" "$@"
