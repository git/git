#!/bin/sh
#
# git-submodule.sh: add, init, update or list git submodules
#
# Copyright (c) 2007 Lars Hjemli

dashless=$(basename "$0" | sed -e 's/-/ /')
USAGE="[--quiet] [--cached]
   or: $dashless [--quiet] add [-b <branch>] [-f|--force] [--name <name>] [--reference <repository>] [--] <repository> [<path>]
   or: $dashless [--quiet] status [--cached] [--recursive] [--] [<path>...]
   or: $dashless [--quiet] init [--] [<path>...]
   or: $dashless [--quiet] deinit [-f|--force] (--all| [--] <path>...)
   or: $dashless [--quiet] update [--init [--filter=<filter-spec>]] [--remote] [-N|--no-fetch] [-f|--force] [--checkout|--merge|--rebase] [--[no-]recommend-shallow] [--reference <repository>] [--recursive] [--[no-]single-branch] [--] [<path>...]
   or: $dashless [--quiet] set-branch (--default|--branch <branch>) [--] <path>
   or: $dashless [--quiet] set-url [--] <path> <newurl>
   or: $dashless [--quiet] summary [--cached|--files] [--summary-limit <n>] [commit] [--] [<path>...]
   or: $dashless [--quiet] foreach [--recursive] <command>
   or: $dashless [--quiet] sync [--recursive] [--] [<path>...]
   or: $dashless [--quiet] absorbgitdirs [--] [<path>...]"
OPTIONS_SPEC=
SUBDIRECTORY_OK=Yes
. git-sh-setup
require_work_tree
wt_prefix=$(git rev-parse --show-prefix)
cd_to_toplevel

# Tell the rest of git that any URLs we get don't come
# directly from the user, so it can apply policy as appropriate.
GIT_PROTOCOL_FROM_USER=0
export GIT_PROTOCOL_FROM_USER

command=
branch=
force=
reference=
cached=
recursive=
init=
require_init=
files=
remote=
nofetch=
update=
prefix=
custom_name=
depth=
progress=
dissociate=
single_branch=
jobs=
recommend_shallow=
filter=

isnumber()
{
	n=$(($1 + 0)) 2>/dev/null && test "$n" = "$1"
}

# Sanitize the local git environment for use within a submodule. We
# can't simply use clear_local_git_env since we want to preserve some
# of the settings from GIT_CONFIG_PARAMETERS.
sanitize_submodule_env()
{
	save_config=$GIT_CONFIG_PARAMETERS
	clear_local_git_env
	GIT_CONFIG_PARAMETERS=$save_config
	export GIT_CONFIG_PARAMETERS
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
	reference_path=
	while test $# -ne 0
	do
		case "$1" in
		-b | --branch)
			case "$2" in '') usage ;; esac
			branch=$2
			shift
			;;
		-f | --force)
			force=$1
			;;
		-q|--quiet)
			GIT_QUIET=1
			;;
		--progress)
			progress=1
			;;
		--reference)
			case "$2" in '') usage ;; esac
			reference_path=$2
			shift
			;;
		--reference=*)
			reference_path="${1#--reference=}"
			;;
		--dissociate)
			dissociate=1
			;;
		--name)
			case "$2" in '') usage ;; esac
			custom_name=$2
			shift
			;;
		--depth)
			case "$2" in '') usage ;; esac
			depth="--depth=$2"
			shift
			;;
		--depth=*)
			depth=$1
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

	if test -z "$1"
	then
		usage
	fi

	git ${wt_prefix:+-C "$wt_prefix"} ${prefix:+--super-prefix "$prefix"} submodule--helper add ${GIT_QUIET:+--quiet} ${force:+--force} ${progress:+"--progress"} ${branch:+--branch "$branch"} ${reference_path:+--reference "$reference_path"} ${dissociate:+--dissociate} ${custom_name:+--name "$custom_name"} ${depth:+"$depth"} -- "$@"
}

#
# Execute an arbitrary command sequence in each checked out
# submodule
#
# $@ = command to execute
#
cmd_foreach()
{
	# parse $args after "submodule ... foreach".
	while test $# -ne 0
	do
		case "$1" in
		-q|--quiet)
			GIT_QUIET=1
			;;
		--recursive)
			recursive=1
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

	git ${wt_prefix:+-C "$wt_prefix"} submodule--helper foreach ${GIT_QUIET:+--quiet} ${recursive:+--recursive} -- "$@"
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
			GIT_QUIET=1
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

	git ${wt_prefix:+-C "$wt_prefix"} ${prefix:+--super-prefix "$prefix"} submodule--helper init ${GIT_QUIET:+--quiet} -- "$@"
}

#
# Unregister submodules from .git/config and remove their work tree
#
cmd_deinit()
{
	# parse $args after "submodule ... deinit".
	deinit_all=
	while test $# -ne 0
	do
		case "$1" in
		-f|--force)
			force=$1
			;;
		-q|--quiet)
			GIT_QUIET=1
			;;
		--all)
			deinit_all=t
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

	git ${wt_prefix:+-C "$wt_prefix"} submodule--helper deinit ${GIT_QUIET:+--quiet} ${force:+--force} ${deinit_all:+--all} -- "$@"
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
			GIT_QUIET=1
			;;
		-v)
			unset GIT_QUIET
			;;
		--progress)
			progress=1
			;;
		-i|--init)
			init=1
			;;
		--require-init)
			init=1
			require_init=1
			;;
		--remote)
			remote=1
			;;
		-N|--no-fetch)
			nofetch=1
			;;
		-f|--force)
			force=$1
			;;
		-r|--rebase)
			update="rebase"
			;;
		--reference)
			case "$2" in '') usage ;; esac
			reference="--reference=$2"
			shift
			;;
		--reference=*)
			reference="$1"
			;;
		--dissociate)
			dissociate=1
			;;
		-m|--merge)
			update="merge"
			;;
		--recursive)
			recursive=1
			;;
		--checkout)
			update="checkout"
			;;
		--recommend-shallow)
			recommend_shallow="--recommend-shallow"
			;;
		--no-recommend-shallow)
			recommend_shallow="--no-recommend-shallow"
			;;
		--depth)
			case "$2" in '') usage ;; esac
			depth="--depth=$2"
			shift
			;;
		--depth=*)
			depth=$1
			;;
		-j|--jobs)
			case "$2" in '') usage ;; esac
			jobs="--jobs=$2"
			shift
			;;
		--jobs=*)
			jobs=$1
			;;
		--single-branch)
			single_branch="--single-branch"
			;;
		--no-single-branch)
			single_branch="--no-single-branch"
			;;
		--filter)
			case "$2" in '') usage ;; esac
			filter="--filter=$2"
			shift
			;;
		--filter=*)
			filter="$1"
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

	git ${wt_prefix:+-C "$wt_prefix"} submodule--helper update \
		${GIT_QUIET:+--quiet} \
		${force:+--force} \
		${progress:+"--progress"} \
		${remote:+--remote} \
		${recursive:+--recursive} \
		${init:+--init} \
		${nofetch:+--no-fetch} \
		${wt_prefix:+--prefix "$wt_prefix"} \
		${prefix:+--recursive-prefix "$prefix"} \
		${update:+--update "$update"} \
		${reference:+"$reference"} \
		${dissociate:+"--dissociate"} \
		${depth:+"$depth"} \
		${require_init:+--require-init} \
		${dissociate:+"--dissociate"} \
		$single_branch \
		$recommend_shallow \
		$jobs \
		$filter \
		-- \
		"$@"
}

#
# Configures a submodule's default branch
#
# $@ = requested path
#
cmd_set_branch() {
	default=
	branch=

	while test $# -ne 0
	do
		case "$1" in
		-q|--quiet)
			# we don't do anything with this but we need to accept it
			;;
		-d|--default)
			default=1
			;;
		-b|--branch)
			case "$2" in '') usage ;; esac
			branch=$2
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

	git ${wt_prefix:+-C "$wt_prefix"} submodule--helper set-branch ${GIT_QUIET:+--quiet} ${branch:+--branch "$branch"} ${default:+--default} -- "$@"
}

#
# Configures a submodule's remote url
#
# $@ = requested path, requested url
#
cmd_set_url() {
	while test $# -ne 0
	do
		case "$1" in
		-q|--quiet)
			GIT_QUIET=1
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

	git ${wt_prefix:+-C "$wt_prefix"} submodule--helper set-url ${GIT_QUIET:+--quiet} -- "$@"
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
	diff_cmd=diff-index

	# parse $args after "submodule ... summary".
	while test $# -ne 0
	do
		case "$1" in
		--cached)
			cached="$1"
			;;
		--files)
			files="$1"
			;;
		--for-status)
			for_status="$1"
			;;
		-n|--summary-limit)
			summary_limit="$2"
			isnumber "$summary_limit" || usage
			shift
			;;
		--summary-limit=*)
			summary_limit="${1#--summary-limit=}"
			isnumber "$summary_limit" || usage
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

	git ${wt_prefix:+-C "$wt_prefix"} submodule--helper summary ${files:+--files} ${cached:+--cached} ${for_status:+--for-status} ${summary_limit:+-n $summary_limit} -- "$@"
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
			GIT_QUIET=1
			;;
		--cached)
			cached=1
			;;
		--recursive)
			recursive=1
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

	git ${wt_prefix:+-C "$wt_prefix"} submodule--helper status ${GIT_QUIET:+--quiet} ${cached:+--cached} ${recursive:+--recursive} -- "$@"
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
			GIT_QUIET=1
			shift
			;;
		--recursive)
			recursive=1
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

	git ${wt_prefix:+-C "$wt_prefix"} submodule--helper sync ${GIT_QUIET:+--quiet} ${recursive:+--recursive} -- "$@"
}

cmd_absorbgitdirs()
{
	git submodule--helper absorb-git-dirs --prefix "$wt_prefix" "$@"
}

# This loop parses the command line arguments to find the
# subcommand name to dispatch.  Parsing of the subcommand specific
# options are primarily done by the subcommand implementations.
# Subcommand specific options such as --branch and --cached are
# parsed here as well, for backward compatibility.

while test $# != 0 && test -z "$command"
do
	case "$1" in
	add | foreach | init | deinit | update | set-branch | set-url | status | summary | sync | absorbgitdirs)
		command=$1
		;;
	-q|--quiet)
		GIT_QUIET=1
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
if test -z "$command"
then
    if test $# = 0
    then
	command=status
    else
	usage
    fi
fi

# "-b branch" is accepted only by "add" and "set-branch"
if test -n "$branch" && (test "$command" != add || test "$command" != set-branch)
then
	usage
fi

# "--cached" is accepted only by "status" and "summary"
if test -n "$cached" && test "$command" != status && test "$command" != summary
then
	usage
fi

"cmd_$(echo $command | sed -e s/-/_/g)" "$@"
