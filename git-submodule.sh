#!/bin/sh
#
# git-submodule.sh: add, init, update or list git submodules
#
# Copyright (c) 2007 Lars Hjemli

dashless=$(basename "$0" | sed -e 's/-/ /')
USAGE="[--quiet] add [-b <branch>] [-f|--force] [--name <name>] [--reference <repository>] [--] <repository> [<path>]
   or: $dashless [--quiet] status [--cached] [--recursive] [--] [<path>...]
   or: $dashless [--quiet] init [--] [<path>...]
   or: $dashless [--quiet] deinit [-f|--force] (--all| [--] <path>...)
   or: $dashless [--quiet] update [--init] [--remote] [-N|--no-fetch] [-f|--force] [--checkout|--merge|--rebase] [--[no-]recommend-shallow] [--reference <repository>] [--recursive] [--] [<path>...]
   or: $dashless [--quiet] summary [--cached|--files] [--summary-limit <n>] [commit] [--] [<path>...]
   or: $dashless [--quiet] foreach [--recursive] <command>
   or: $dashless [--quiet] sync [--recursive] [--] [<path>...]
   or: $dashless [--quiet] absorbgitdirs [--] [<path>...]"
OPTIONS_SPEC=
SUBDIRECTORY_OK=Yes
. git-sh-setup
. git-parse-remote
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
files=
remote=
nofetch=
update=
prefix=
custom_name=
depth=
progress=

die_if_unmatched ()
{
	if test "$1" = "#unmatched"
	then
		exit ${2:-1}
	fi
}

#
# Print a submodule configuration setting
#
# $1 = submodule name
# $2 = option name
# $3 = default value
#
# Checks in the usual git-config places first (for overrides),
# otherwise it falls back on .gitmodules.  This allows you to
# distribute project-wide defaults in .gitmodules, while still
# customizing individual repositories if necessary.  If the option is
# not in .gitmodules either, print a default value.
#
get_submodule_config () {
	name="$1"
	option="$2"
	default="$3"
	value=$(git config submodule."$name"."$option")
	if test -z "$value"
	then
		value=$(git config -f .gitmodules submodule."$name"."$option")
	fi
	printf '%s' "${value:-$default}"
}

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
		--reference)
			case "$2" in '') usage ;; esac
			reference_path=$2
			shift
			;;
		--reference=*)
			reference_path="${1#--reference=}"
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
		die "$(gettext "Relative path can only be used from the toplevel of the working tree")"

		# dereference source url relative to parent's url
		realrepo=$(git submodule--helper resolve-relative-url "$repo") || exit
		;;
	*:*|/*)
		# absolute url
		realrepo=$repo
		;;
	*)
		die "$(eval_gettext "repo URL: '\$repo' must be absolute or begin with ./|../")"
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
		die "$(eval_gettext "'\$sm_path' already exists in the index")"
	else
		git ls-files -s "$sm_path" | sane_grep -v "^160000" > /dev/null 2>&1 &&
		die "$(eval_gettext "'\$sm_path' already exists in the index and is not a submodule")"
	fi

	if test -z "$force" &&
		! git add --dry-run --ignore-missing --no-warn-embedded-repo "$sm_path" > /dev/null 2>&1
	then
		eval_gettextln "The following path is ignored by one of your .gitignore files:
\$sm_path
Use -f if you really want to add it." >&2
		exit 1
	fi

	if test -n "$custom_name"
	then
		sm_name="$custom_name"
	else
		sm_name="$sm_path"
	fi

	# perhaps the path exists and is already a git repo, else clone it
	if test -e "$sm_path"
	then
		if test -d "$sm_path"/.git || test -f "$sm_path"/.git
		then
			eval_gettextln "Adding existing repo at '\$sm_path' to the index"
		else
			die "$(eval_gettext "'\$sm_path' already exists and is not a valid git repo")"
		fi

	else
		if test -d ".git/modules/$sm_name"
		then
			if test -z "$force"
			then
				eval_gettextln >&2 "A git directory for '\$sm_name' is found locally with remote(s):"
				GIT_DIR=".git/modules/$sm_name" GIT_WORK_TREE=. git remote -v | grep '(fetch)' | sed -e s,^,"  ", -e s,' (fetch)',, >&2
				die "$(eval_gettextln "\
If you want to reuse this local git directory instead of cloning again from
  \$realrepo
use the '--force' option. If the local git directory is not the correct repo
or you are unsure what this means choose another name with the '--name' option.")"
			else
				eval_gettextln "Reactivating local git directory for submodule '\$sm_name'."
			fi
		fi
		git submodule--helper clone ${GIT_QUIET:+--quiet} --prefix "$wt_prefix" --path "$sm_path" --name "$sm_name" --url "$realrepo" ${reference:+"$reference"} ${depth:+"$depth"} || exit
		(
			sanitize_submodule_env
			cd "$sm_path" &&
			# ash fails to wordsplit ${branch:+-b "$branch"...}
			case "$branch" in
			'') git checkout -f -q ;;
			?*) git checkout -f -q -B "$branch" "origin/$branch" ;;
			esac
		) || die "$(eval_gettext "Unable to checkout submodule '\$sm_path'")"
	fi
	git config submodule."$sm_name".url "$realrepo"

	git add --no-warn-embedded-repo $force "$sm_path" ||
	die "$(eval_gettext "Failed to add submodule '\$sm_path'")"

	git config -f .gitmodules submodule."$sm_name".path "$sm_path" &&
	git config -f .gitmodules submodule."$sm_name".url "$repo" &&
	if test -n "$branch"
	then
		git config -f .gitmodules submodule."$sm_name".branch "$branch"
	fi &&
	git add --force .gitmodules ||
	die "$(eval_gettext "Failed to register submodule '\$sm_path'")"

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

	toplevel=$(pwd)

	# dup stdin so that it can be restored when running the external
	# command in the subshell (and a recursive call to this function)
	exec 3<&0

	{
		git submodule--helper list --prefix "$wt_prefix" ||
		echo "#unmatched" $?
	} |
	while read -r mode sha1 stage sm_path
	do
		die_if_unmatched "$mode" "$sha1"
		if test -e "$sm_path"/.git
		then
			displaypath=$(git submodule--helper relative-path "$prefix$sm_path" "$wt_prefix")
			say "$(eval_gettext "Entering '\$displaypath'")"
			name=$(git submodule--helper name "$sm_path")
			(
				prefix="$prefix$sm_path/"
				sanitize_submodule_env
				cd "$sm_path" &&
				sm_path=$(git submodule--helper relative-path "$sm_path" "$wt_prefix") &&
				# we make $path available to scripts ...
				path=$sm_path &&
				if test $# -eq 1
				then
					eval "$1"
				else
					"$@"
				fi &&
				if test -n "$recursive"
				then
					cmd_foreach "--recursive" "$@"
				fi
			) <&3 3<&- ||
			die "$(eval_gettext "Stopping at '\$displaypath'; script returned non-zero status.")"
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

	git ${wt_prefix:+-C "$wt_prefix"} ${prefix:+--super-prefix "$prefix"} submodule--helper init ${GIT_QUIET:+--quiet}  "$@"
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

	if test -n "$deinit_all" && test "$#" -ne 0
	then
		echo >&2 "$(eval_gettext "pathspec and --all are incompatible")"
		usage
	fi
	if test $# = 0 && test -z "$deinit_all"
	then
		die "$(eval_gettext "Use '--all' if you really want to deinitialize all submodules")"
	fi

	{
		git submodule--helper list --prefix "$wt_prefix" "$@" ||
		echo "#unmatched" $?
	} |
	while read -r mode sha1 stage sm_path
	do
		die_if_unmatched "$mode" "$sha1"
		name=$(git submodule--helper name "$sm_path") || exit

		displaypath=$(git submodule--helper relative-path "$sm_path" "$wt_prefix")

		# Remove the submodule work tree (unless the user already did it)
		if test -d "$sm_path"
		then
			# Protect submodules containing a .git directory
			if test -d "$sm_path/.git"
			then
				die "$(eval_gettext "\
Submodule work tree '\$displaypath' contains a .git directory
(use 'rm -rf' if you really want to remove it including all of its history)")"
			fi

			if test -z "$force"
			then
				git rm -qn "$sm_path" ||
				die "$(eval_gettext "Submodule work tree '\$displaypath' contains local modifications; use '-f' to discard them")"
			fi
			rm -rf "$sm_path" &&
			say "$(eval_gettext "Cleared directory '\$displaypath'")" ||
			say "$(eval_gettext "Could not remove submodule work tree '\$displaypath'")"
		fi

		mkdir "$sm_path" || say "$(eval_gettext "Could not create empty submodule directory '\$displaypath'")"

		# Remove the .git/config entries (unless the user already did it)
		if test -n "$(git config --get-regexp submodule."$name\.")"
		then
			# Remove the whole section so we have a clean state when
			# the user later decides to init this submodule again
			url=$(git config submodule."$name".url)
			git config --remove-section submodule."$name" 2>/dev/null &&
			say "$(eval_gettext "Submodule '\$name' (\$url) unregistered for path '\$displaypath'")"
		fi
	done
}

is_tip_reachable () (
	sanitize_submodule_env &&
	cd "$1" &&
	rev=$(git rev-list -n 1 "$2" --not --all 2>/dev/null) &&
	test -z "$rev"
)

fetch_in_submodule () (
	sanitize_submodule_env &&
	cd "$1" &&
	case "$2" in
	'')
		git fetch ;;
	*)
		shift
		git fetch $(get_default_remote) "$@" ;;
	esac
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
		--progress)
			progress="--progress"
			;;
		-i|--init)
			init=1
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
		${progress:+"$progress"} \
		${wt_prefix:+--prefix "$wt_prefix"} \
		${prefix:+--recursive-prefix "$prefix"} \
		${update:+--update "$update"} \
		${reference:+"$reference"} \
		${depth:+--depth "$depth"} \
		${recommend_shallow:+"$recommend_shallow"} \
		${jobs:+$jobs} \
		"$@" || echo "#unmatched" $?
	} | {
	err=
	while read -r mode sha1 stage just_cloned sm_path
	do
		die_if_unmatched "$mode" "$sha1"

		name=$(git submodule--helper name "$sm_path") || exit
		if ! test -z "$update"
		then
			update_module=$update
		else
			update_module=$(git config submodule."$name".update)
			if test -z "$update_module"
			then
				update_module="checkout"
			fi
		fi

		displaypath=$(git submodule--helper relative-path "$prefix$sm_path" "$wt_prefix")

		if test $just_cloned -eq 1
		then
			subsha1=
			case "$update_module" in
			merge | rebase | none)
				update_module=checkout ;;
			esac
		else
			subsha1=$(sanitize_submodule_env; cd "$sm_path" &&
				git rev-parse --verify HEAD) ||
			die "$(eval_gettext "Unable to find current revision in submodule path '\$displaypath'")"
		fi

		if test -n "$remote"
		then
			branch=$(git submodule--helper remote-branch "$sm_path")
			if test -z "$nofetch"
			then
				# Fetch remote before determining tracking $sha1
				fetch_in_submodule "$sm_path" $depth ||
				die "$(eval_gettext "Unable to fetch in submodule path '\$sm_path'")"
			fi
			remote_name=$(sanitize_submodule_env; cd "$sm_path" && get_default_remote)
			sha1=$(sanitize_submodule_env; cd "$sm_path" &&
				git rev-parse --verify "${remote_name}/${branch}") ||
			die "$(eval_gettext "Unable to find current \${remote_name}/\${branch} revision in submodule path '\$sm_path'")"
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
				die "$(eval_gettext "Unable to fetch in submodule path '\$displaypath'")"

				# Now we tried the usual fetch, but $sha1 may
				# not be reachable from any of the refs
				is_tip_reachable "$sm_path" "$sha1" ||
				fetch_in_submodule "$sm_path" $depth "$sha1" ||
				die "$(eval_gettext "Fetched in submodule path '\$displaypath', but it did not contain \$sha1. Direct fetching of that commit failed.")"
			fi

			must_die_on_failure=
			case "$update_module" in
			checkout)
				command="git checkout $subforce -q"
				die_msg="$(eval_gettext "Unable to checkout '\$sha1' in submodule path '\$displaypath'")"
				say_msg="$(eval_gettext "Submodule path '\$displaypath': checked out '\$sha1'")"
				;;
			rebase)
				command="git rebase"
				die_msg="$(eval_gettext "Unable to rebase '\$sha1' in submodule path '\$displaypath'")"
				say_msg="$(eval_gettext "Submodule path '\$displaypath': rebased into '\$sha1'")"
				must_die_on_failure=yes
				;;
			merge)
				command="git merge"
				die_msg="$(eval_gettext "Unable to merge '\$sha1' in submodule path '\$displaypath'")"
				say_msg="$(eval_gettext "Submodule path '\$displaypath': merged in '\$sha1'")"
				must_die_on_failure=yes
				;;
			!*)
				command="${update_module#!}"
				die_msg="$(eval_gettext "Execution of '\$command \$sha1' failed in submodule path '\$displaypath'")"
				say_msg="$(eval_gettext "Submodule path '\$displaypath': '\$command \$sha1'")"
				must_die_on_failure=yes
				;;
			*)
				die "$(eval_gettext "Invalid update mode '$update_module' for submodule '$name'")"
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
				die_msg="$(eval_gettext "Failed to recurse into submodule path '\$displaypath'")"
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

	test $summary_limit = 0 && return

	if rev=$(git rev-parse -q --verify --default HEAD ${1+"$1"})
	then
		head=$rev
		test $# = 0 || shift
	elif test -z "$1" || test "$1" = "HEAD"
	then
		# before the first commit: compare with an empty tree
		head=$(git hash-object -w -t tree --stdin </dev/null)
		test -z "$1" || shift
	else
		head="HEAD"
	fi

	if [ -n "$files" ]
	then
		test -n "$cached" &&
		die "$(gettext "The --cached option cannot be used with the --files option")"
		diff_cmd=diff-files
		head=
	fi

	cd_to_toplevel
	eval "set $(git rev-parse --sq --prefix "$wt_prefix" -- "$@")"
	# Get modified modules cared by user
	modules=$(git $diff_cmd $cached --ignore-submodules=dirty --raw $head -- "$@" |
		sane_egrep '^:([0-7]* )?160000' |
		while read -r mod_src mod_dst sha1_src sha1_dst status sm_path
		do
			# Always show modules deleted or type-changed (blob<->module)
			if test "$status" = D || test "$status" = T
			then
				printf '%s\n' "$sm_path"
				continue
			fi
			# Respect the ignore setting for --for-status.
			if test -n "$for_status"
			then
				name=$(git submodule--helper name "$sm_path")
				ignore_config=$(get_submodule_config "$name" ignore none)
				test $status != A && test $ignore_config = all && continue
			fi
			# Also show added or modified modules which are checked out
			GIT_DIR="$sm_path/.git" git rev-parse --git-dir >/dev/null 2>&1 &&
			printf '%s\n' "$sm_path"
		done
	)

	test -z "$modules" && return

	git $diff_cmd $cached --ignore-submodules=dirty --raw $head -- $modules |
	sane_egrep '^:([0-7]* )?160000' |
	cut -c2- |
	while read -r mod_src mod_dst sha1_src sha1_dst status name
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
				eval_gettextln "unexpected mode \$mod_dst" >&2
				continue ;;
			esac
		fi
		missing_src=
		missing_dst=

		test $mod_src = 160000 &&
		! GIT_DIR="$name/.git" git rev-parse -q --verify $sha1_src^0 >/dev/null &&
		missing_src=t

		test $mod_dst = 160000 &&
		! GIT_DIR="$name/.git" git rev-parse -q --verify $sha1_dst^0 >/dev/null &&
		missing_dst=t

		display_name=$(git submodule--helper relative-path "$name" "$wt_prefix")

		total_commits=
		case "$missing_src,$missing_dst" in
		t,)
			errmsg="$(eval_gettext "  Warn: \$display_name doesn't contain commit \$sha1_src")"
			;;
		,t)
			errmsg="$(eval_gettext "  Warn: \$display_name doesn't contain commit \$sha1_dst")"
			;;
		t,t)
			errmsg="$(eval_gettext "  Warn: \$display_name doesn't contain commits \$sha1_src and \$sha1_dst")"
			;;
		*)
			errmsg=
			total_commits=$(
			if test $mod_src = 160000 && test $mod_dst = 160000
			then
				range="$sha1_src...$sha1_dst"
			elif test $mod_src = 160000
			then
				range=$sha1_src
			else
				range=$sha1_dst
			fi
			GIT_DIR="$name/.git" \
			git rev-list --first-parent $range -- | wc -l
			)
			total_commits=" ($(($total_commits + 0)))"
			;;
		esac

		sha1_abbr_src=$(echo $sha1_src | cut -c1-7)
		sha1_abbr_dst=$(echo $sha1_dst | cut -c1-7)
		if test $status = T
		then
			blob="$(gettext "blob")"
			submodule="$(gettext "submodule")"
			if test $mod_dst = 160000
			then
				echo "* $display_name $sha1_abbr_src($blob)->$sha1_abbr_dst($submodule)$total_commits:"
			else
				echo "* $display_name $sha1_abbr_src($submodule)->$sha1_abbr_dst($blob)$total_commits:"
			fi
		else
			echo "* $display_name $sha1_abbr_src...$sha1_abbr_dst$total_commits:"
		fi
		if test -n "$errmsg"
		then
			# Don't give error msg for modification whose dst is not submodule
			# i.e. deleted or changed to blob
			test $mod_dst = 160000 && echo "$errmsg"
		else
			if test $mod_src = 160000 && test $mod_dst = 160000
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
	done
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

	git ${wt_prefix:+-C "$wt_prefix"} ${prefix:+--super-prefix "$prefix"} submodule--helper status ${GIT_QUIET:+--quiet} ${cached:+--cached} ${recursive:+--recursive} "$@"
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
	cd_to_toplevel
	{
		git submodule--helper list --prefix "$wt_prefix" "$@" ||
		echo "#unmatched" $?
	} |
	while read -r mode sha1 stage sm_path
	do
		die_if_unmatched "$mode" "$sha1"

		# skip inactive submodules
		if ! git submodule--helper is-active "$sm_path"
		then
			continue
		fi

		name=$(git submodule--helper name "$sm_path")
		url=$(git config -f .gitmodules --get submodule."$name".url)

		# Possibly a url relative to parent
		case "$url" in
		./*|../*)
			# rewrite foo/bar as ../.. to find path from
			# submodule work tree to superproject work tree
			up_path="$(printf '%s\n' "$sm_path" | sed "s/[^/][^/]*/../g")" &&
			# guarantee a trailing /
			up_path=${up_path%/}/ &&
			# path from submodule work tree to submodule origin repo
			sub_origin_url=$(git submodule--helper resolve-relative-url "$url" "$up_path") &&
			# path from superproject work tree to submodule origin repo
			super_config_url=$(git submodule--helper resolve-relative-url "$url") || exit
			;;
		*)
			sub_origin_url="$url"
			super_config_url="$url"
			;;
		esac

		displaypath=$(git submodule--helper relative-path "$prefix$sm_path" "$wt_prefix")
		say "$(eval_gettext "Synchronizing submodule url for '\$displaypath'")"
		git config submodule."$name".url "$super_config_url"

		if test -e "$sm_path"/.git
		then
		(
			sanitize_submodule_env
			cd "$sm_path"
			remote=$(get_default_remote)
			git config remote."$remote".url "$sub_origin_url"

			if test -n "$recursive"
			then
				prefix="$prefix$sm_path/"
				eval cmd_sync
			fi
		)
		fi
	done
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
	add | foreach | init | deinit | update | status | summary | sync | absorbgitdirs)
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

# "-b branch" is accepted only by "add"
if test -n "$branch" && test "$command" != add
then
	usage
fi

# "--cached" is accepted only by "status" and "summary"
if test -n "$cached" && test "$command" != status && test "$command" != summary
then
	usage
fi

"cmd_$command" "$@"
