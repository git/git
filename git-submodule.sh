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
   or: $dashless [--quiet] update [--init] [--remote] [-N|--no-fetch] [-f|--force] [--checkout|--merge|--rebase] [--[no-]recommend-shallow] [--reference <repository>] [--recursive] [--[no-]single-branch] [--] [<path>...]
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

die_if_unmatched ()
{
	if test "$1" = "#unmatched"
	then
		exit ${2:-1}
	fi
}

isnumber()
{
	n=$(($1 + 0)) 2>/dev/null && test "$n" = "$1"
}

# Given a full hex object ID, is this the zero OID?
is_zero_oid () {
	echo "$1" | sane_egrep '^0+$' >/dev/null 2>&1
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

	if ! git submodule--helper config --check-writeable >/dev/null 2>&1
	then
		 die "fatal: $(eval_gettext "please make sure that the .gitmodules file is in the working tree")"
	fi

	if test -n "$reference_path"
	then
		is_absolute_path "$reference_path" ||
		reference_path="$wt_prefix$reference_path"

		reference="--reference=$reference_path"
	fi

	repo=$1
	sm_path=$2

	if test -z "$sm_path"; then
		sm_path=$(printf '%s\n' "$repo" |
			sed -e 's|/$||' -e 's|:*/*\.git$||' -e 's|.*[/:]||g')
	fi

	if test -z "$repo" || test -z "$sm_path"; then
		usage
	fi

	is_absolute_path "$sm_path" || sm_path="$wt_prefix$sm_path"

	# assure repo is absolute or relative to parent
	case "$repo" in
	./*|../*)
		test -z "$wt_prefix" ||
		die "fatal: $(gettext "Relative path can only be used from the toplevel of the working tree")"

		# dereference source url relative to parent's url
		realrepo=$(git submodule--helper resolve-relative-url "$repo") || exit
		;;
	*:*|/*)
		# absolute url
		realrepo=$repo
		;;
	*)
		die "fatal: $(eval_gettext "repo URL: '\$repo' must be absolute or begin with ./|../")"
	;;
	esac

	# normalize path:
	# multiple //; leading ./; /./; /../; trailing /
	sm_path=$(printf '%s/\n' "$sm_path" |
		sed -e '
			s|//*|/|g
			s|^\(\./\)*||
			s|/\(\./\)*|/|g
			:start
			s|\([^/]*\)/\.\./||
			tstart
			s|/*$||
		')
	if test -z "$force"
	then
		git ls-files --error-unmatch "$sm_path" > /dev/null 2>&1 &&
		die "fatal: $(eval_gettext "'\$sm_path' already exists in the index")"
	else
		git ls-files -s "$sm_path" | sane_grep -v "^160000" > /dev/null 2>&1 &&
		die "fatal: $(eval_gettext "'\$sm_path' already exists in the index and is not a submodule")"
	fi

	if test -d "$sm_path" &&
		test -z $(git -C "$sm_path" rev-parse --show-cdup 2>/dev/null)
	then
	    git -C "$sm_path" rev-parse --verify -q HEAD >/dev/null ||
	    die "fatal: $(eval_gettext "'\$sm_path' does not have a commit checked out")"
	fi

	if test -z "$force"
	then
	    dryerr=$(git add --dry-run --ignore-missing --no-warn-embedded-repo "$sm_path" 2>&1 >/dev/null)
	    res=$?
	    if test $res -ne 0
	    then
		 echo >&2 "$dryerr"
		 exit $res
	    fi
	fi

	if test -n "$custom_name"
	then
		sm_name="$custom_name"
	else
		sm_name="$sm_path"
	fi

	if ! git submodule--helper check-name "$sm_name"
	then
		die "fatal: $(eval_gettext "'$sm_name' is not a valid submodule name")"
	fi

	git submodule--helper add-clone ${GIT_QUIET:+--quiet} ${force:+"--force"} ${progress:+"--progress"} ${branch:+--branch "$branch"} --prefix "$wt_prefix" --path "$sm_path" --name "$sm_name" --url "$realrepo" ${reference:+"$reference"} ${dissociate:+"--dissociate"} ${depth:+"$depth"} || exit
	git config submodule."$sm_name".url "$realrepo"

	git add --no-warn-embedded-repo $force "$sm_path" ||
	die "fatal: $(eval_gettext "Failed to add submodule '\$sm_path'")"

	git submodule--helper config submodule."$sm_name".path "$sm_path" &&
	git submodule--helper config submodule."$sm_name".url "$repo" &&
	if test -n "$branch"
	then
		git submodule--helper config submodule."$sm_name".branch "$branch"
	fi &&
	git add --force .gitmodules ||
	die "fatal: $(eval_gettext "Failed to register submodule '\$sm_path'")"

	# NEEDSWORK: In a multi-working-tree world, this needs to be
	# set in the per-worktree config.
	if git config --get submodule.active >/dev/null
	then
		# If the submodule being adding isn't already covered by the
		# current configured pathspec, set the submodule's active flag
		if ! git submodule--helper is-active "$sm_path"
		then
			git config submodule."$sm_name".active "true"
		fi
	else
		git config submodule."$sm_name".active "true"
	fi
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

is_tip_reachable () (
	sanitize_submodule_env &&
	cd "$1" &&
	rev=$(git rev-list -n 1 "$2" --not --all 2>/dev/null) &&
	test -z "$rev"
)

# usage: fetch_in_submodule <module_path> [<depth>] [<sha1>]
# Because arguments are positional, use an empty string to omit <depth>
# but include <sha1>.
fetch_in_submodule () (
	sanitize_submodule_env &&
	cd "$1" &&
	if test $# -eq 3
	then
		echo "$3" | git fetch ${GIT_QUIET:+--quiet} --stdin ${2:+"$2"}
	else
		git fetch ${GIT_QUIET:+--quiet} ${2:+"$2"}
	fi
)

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

	if test -n "$init"
	then
		cmd_init "--" "$@" || return
	fi

	{
	git submodule--helper update-clone ${GIT_QUIET:+--quiet} \
		${progress:+"--progress"} \
		${wt_prefix:+--prefix "$wt_prefix"} \
		${prefix:+--recursive-prefix "$prefix"} \
		${update:+--update "$update"} \
		${reference:+"$reference"} \
		${dissociate:+"--dissociate"} \
		${depth:+--depth "$depth"} \
		${require_init:+--require-init} \
		$single_branch \
		$recommend_shallow \
		$jobs \
		-- \
		"$@" || echo "#unmatched" $?
	} | {
	err=
	while read -r quickabort sha1 just_cloned sm_path
	do
		die_if_unmatched "$quickabort" "$sha1"

		git submodule--helper ensure-core-worktree "$sm_path" || exit 1

		update_module=$(git submodule--helper update-module-mode $just_cloned "$sm_path" $update)

		displaypath=$(git submodule--helper relative-path "$prefix$sm_path" "$wt_prefix")

		if test $just_cloned -eq 1
		then
			subsha1=
		else
			subsha1=$(sanitize_submodule_env; cd "$sm_path" &&
				git rev-parse --verify HEAD) ||
			die "fatal: $(eval_gettext "Unable to find current revision in submodule path '\$displaypath'")"
		fi

		if test -n "$remote"
		then
			branch=$(git submodule--helper remote-branch "$sm_path")
			if test -z "$nofetch"
			then
				# Fetch remote before determining tracking $sha1
				fetch_in_submodule "$sm_path" $depth ||
				die "fatal: $(eval_gettext "Unable to fetch in submodule path '\$sm_path'")"
			fi
			remote_name=$(sanitize_submodule_env; cd "$sm_path" && git submodule--helper print-default-remote)
			sha1=$(sanitize_submodule_env; cd "$sm_path" &&
				git rev-parse --verify "${remote_name}/${branch}") ||
			die "fatal: $(eval_gettext "Unable to find current \${remote_name}/\${branch} revision in submodule path '\$sm_path'")"
		fi

		if test "$subsha1" != "$sha1" || test -n "$force"
		then
			subforce=$force
			# If we don't already have a -f flag and the submodule has never been checked out
			if test -z "$subsha1" && test -z "$force"
			then
				subforce="-f"
			fi

			if test -z "$nofetch"
			then
				# Run fetch only if $sha1 isn't present or it
				# is not reachable from a ref.
				is_tip_reachable "$sm_path" "$sha1" ||
				fetch_in_submodule "$sm_path" $depth ||
				say "$(eval_gettext "Unable to fetch in submodule path '\$displaypath'; trying to directly fetch \$sha1:")"

				# Now we tried the usual fetch, but $sha1 may
				# not be reachable from any of the refs
				is_tip_reachable "$sm_path" "$sha1" ||
				fetch_in_submodule "$sm_path" "$depth" "$sha1" ||
				die "fatal: $(eval_gettext "Fetched in submodule path '\$displaypath', but it did not contain \$sha1. Direct fetching of that commit failed.")"
			fi

			must_die_on_failure=
			case "$update_module" in
			checkout)
				command="git checkout $subforce -q"
				die_msg="fatal: $(eval_gettext "Unable to checkout '\$sha1' in submodule path '\$displaypath'")"
				say_msg="$(eval_gettext "Submodule path '\$displaypath': checked out '\$sha1'")"
				;;
			rebase)
				command="git rebase ${GIT_QUIET:+--quiet}"
				die_msg="fatal: $(eval_gettext "Unable to rebase '\$sha1' in submodule path '\$displaypath'")"
				say_msg="$(eval_gettext "Submodule path '\$displaypath': rebased into '\$sha1'")"
				must_die_on_failure=yes
				;;
			merge)
				command="git merge ${GIT_QUIET:+--quiet}"
				die_msg="fatal: $(eval_gettext "Unable to merge '\$sha1' in submodule path '\$displaypath'")"
				say_msg="$(eval_gettext "Submodule path '\$displaypath': merged in '\$sha1'")"
				must_die_on_failure=yes
				;;
			!*)
				command="${update_module#!}"
				die_msg="fatal: $(eval_gettext "Execution of '\$command \$sha1' failed in submodule path '\$displaypath'")"
				say_msg="$(eval_gettext "Submodule path '\$displaypath': '\$command \$sha1'")"
				must_die_on_failure=yes
				;;
			*)
				die "fatal: $(eval_gettext "Invalid update mode '$update_module' for submodule path '$path'")"
			esac

			if (sanitize_submodule_env; cd "$sm_path" && $command "$sha1")
			then
				say "$say_msg"
			elif test -n "$must_die_on_failure"
			then
				die_with_status 2 "$die_msg"
			else
				err="${err};$die_msg"
				continue
			fi
		fi

		if test -n "$recursive"
		then
			(
				prefix=$(git submodule--helper relative-path "$prefix$sm_path/" "$wt_prefix")
				wt_prefix=
				sanitize_submodule_env
				cd "$sm_path" &&
				eval cmd_update
			)
			res=$?
			if test $res -gt 0
			then
				die_msg="fatal: $(eval_gettext "Failed to recurse into submodule path '\$displaypath'")"
				if test $res -ne 2
				then
					err="${err};$die_msg"
					continue
				else
					die_with_status $res "$die_msg"
				fi
			fi
		fi
	done

	if test -n "$err"
	then
		OIFS=$IFS
		IFS=';'
		for e in $err
		do
			if test -n "$e"
			then
				echo >&2 "$e"
			fi
		done
		IFS=$OIFS
		exit 1
	fi
	}
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
