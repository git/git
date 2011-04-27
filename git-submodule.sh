#!/bin/sh
#
# git-submodule.sh: add, init, update or list git submodules
#
# Copyright (c) 2007 Lars Hjemli

dashless=$(basename "$0" | sed -e 's/-/ /')
USAGE="[--quiet] add [-b <branch>] [-f|--force] [--name <name>] [--reference <repository>] [--] <repository> [<path>]
   or: $dashless [--quiet] status [--cached] [--recursive] [--] [<path>...]
   or: $dashless [--quiet] init [--] [<path>...]
   or: $dashless [--quiet] update [--init] [-N|--no-fetch] [-f|--force] [--rebase] [--reference <repository>] [--merge] [--recursive] [--] [<path>...]
   or: $dashless [--quiet] summary [--cached|--files] [--summary-limit <n>] [commit] [--] [<path>...]
   or: $dashless [--quiet] foreach [--recursive] <command>
   or: $dashless [--quiet] sync [--recursive] [--] [<path>...]"
OPTIONS_SPEC=
. git-sh-setup
. git-sh-i18n
. git-parse-remote
require_work_tree

command=
branch=
force=
reference=
cached=
recursive=
init=
files=
nofetch=
update=
prefix=
custom_name=

# The function takes at most 2 arguments. The first argument is the
# URL that navigates to the submodule origin repo. When relative, this URL
# is relative to the superproject origin URL repo. The second up_path
# argument, if specified, is the relative path that navigates
# from the submodule working tree to the superproject working tree.
#
# The output of the function is the origin URL of the submodule.
#
# The output will either be an absolute URL or filesystem path (if the
# superproject origin URL is an absolute URL or filesystem path,
# respectively) or a relative file system path (if the superproject
# origin URL is a relative file system path).
#
# When the output is a relative file system path, the path is either
# relative to the submodule working tree, if up_path is specified, or to
# the superproject working tree otherwise.
resolve_relative_url ()
{
	remote=$(get_default_remote)
	remoteurl=$(git config "remote.$remote.url") ||
		remoteurl=$(pwd) # the repository is its own authoritative upstream
	url="$1"
	remoteurl=${remoteurl%/}
	sep=/
	up_path="$2"

	case "$remoteurl" in
	*:*|/*)
		is_relative=
		;;
	./*|../*)
		is_relative=t
		;;
	*)
		is_relative=t
		remoteurl="./$remoteurl"
		;;
	esac

	while test -n "$url"
	do
		case "$url" in
		../*)
			url="${url#../}"
			case "$remoteurl" in
			*/*)
				remoteurl="${remoteurl%/*}"
				;;
			*:*)
				remoteurl="${remoteurl%:*}"
				sep=:
				;;
			*)
				if test -z "$is_relative" || test "." = "$remoteurl"
				then
					die "$(eval_gettext "cannot strip one component off url '\$remoteurl'")"
				else
					remoteurl=.
				fi
				;;
			esac
			;;
		./*)
			url="${url#./}"
			;;
		*)
			break;;
		esac
	done
	remoteurl="$remoteurl$sep${url%/}"
	echo "${is_relative:+${up_path}}${remoteurl#./}"
}

#
# Get submodule info for registered submodules
# $@ = path to limit submodule list
#
module_list()
{
	(
		git ls-files --error-unmatch --stage -- "$@" ||
		echo "unmatched pathspec exists"
	) |
	perl -e '
	my %unmerged = ();
	my ($null_sha1) = ("0" x 40);
	my @out = ();
	my $unmatched = 0;
	while (<STDIN>) {
		if (/^unmatched pathspec/) {
			$unmatched = 1;
			next;
		}
		chomp;
		my ($mode, $sha1, $stage, $path) =
			/^([0-7]+) ([0-9a-f]{40}) ([0-3])\t(.*)$/;
		next unless $mode eq "160000";
		if ($stage ne "0") {
			if (!$unmerged{$path}++) {
				push @out, "$mode $null_sha1 U\t$path\n";
			}
			next;
		}
		push @out, "$_\n";
	}
	if ($unmatched) {
		print "#unmatched\n";
	} else {
		print for (@out);
	}
	'
}

die_if_unmatched ()
{
	if test "$1" = "#unmatched"
	then
		exit 1
	fi
}

#
# Map submodule path to submodule name
#
# $1 = path
#
module_name()
{
	# Do we have "submodule.<something>.path = $1" defined in .gitmodules file?
	sm_path="$1"
	re=$(printf '%s\n' "$1" | sed -e 's/[].[^$\\*]/\\&/g')
	name=$( git config -f .gitmodules --get-regexp '^submodule\..*\.path$' |
		sed -n -e 's|^submodule\.\(.*\)\.path '"$re"'$|\1|p' )
	test -z "$name" &&
	die "$(eval_gettext "No submodule mapping found in .gitmodules for path '\$sm_path'")"
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
	sm_path=$1
	name=$2
	url=$3
	reference="$4"
	quiet=
	if test -n "$GIT_QUIET"
	then
		quiet=-q
	fi

	gitdir=
	gitdir_base=
	base_name=$(dirname "$name")

	gitdir=$(git rev-parse --git-dir)
	gitdir_base="$gitdir/modules/$base_name"
	gitdir="$gitdir/modules/$name"

	if test -d "$gitdir"
	then
		mkdir -p "$sm_path"
		rm -f "$gitdir/index"
	else
		mkdir -p "$gitdir_base"
		(
			clear_local_git_env
			git clone $quiet -n ${reference:+"$reference"} \
				--separate-git-dir "$gitdir" "$url" "$sm_path"
		) ||
		die "$(eval_gettext "Clone of '\$url' into submodule path '\$sm_path' failed")"
	fi

	# We already are at the root of the work tree but cd_to_toplevel will
	# resolve any symlinks that might be present in $PWD
	a=$(cd_to_toplevel && cd "$gitdir" && pwd)/
	b=$(cd_to_toplevel && cd "$sm_path" && pwd)/
	# Remove all common leading directories after a sanity check
	if test "${a#$b}" != "$a" || test "${b#$a}" != "$b"; then
		die "$(eval_gettext "Gitdir '\$a' is part of the submodule path '\$b' or vice versa")"
	fi
	while test "${a%%/*}" = "${b%%/*}"
	do
		a=${a#*/}
		b=${b#*/}
	done
	# Now chop off the trailing '/'s that were added in the beginning
	a=${a%/}
	b=${b%/}

	# Turn each leading "*/" component into "../"
	rel=$(echo $b | sed -e 's|[^/][^/]*|..|g')
	echo "gitdir: $rel/$a" >"$sm_path/.git"

	rel=$(echo $a | sed -e 's|[^/][^/]*|..|g')
	(clear_local_git_env; cd "$sm_path" && GIT_WORK_TREE=. git config core.worktree "$rel/$b")
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
		-f | --force)
			force=$1
			;;
		-q|--quiet)
			GIT_QUIET=1
			;;
		--reference)
			case "$2" in '') usage ;; esac
			reference="--reference=$2"
			shift
			;;
		--reference=*)
			reference="$1"
			;;
		--name)
			case "$2" in '') usage ;; esac
			custom_name=$2
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

	repo=$1
	sm_path=$2

	if test -z "$sm_path"; then
		sm_path=$(echo "$repo" |
			sed -e 's|/$||' -e 's|:*/*\.git$||' -e 's|.*[/:]||g')
	fi

	if test -z "$repo" -o -z "$sm_path"; then
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
		die "$(eval_gettext "repo URL: '\$repo' must be absolute or begin with ./|../")"
	;;
	esac

	# normalize path:
	# multiple //; leading ./; /./; /../; trailing /
	sm_path=$(printf '%s/\n' "$sm_path" |
		sed -e '
			s|//*|/|g
			s|^\(\./\)*||
			s|/\./|/|g
			:start
			s|\([^/]*\)/\.\./||
			tstart
			s|/*$||
		')
	git ls-files --error-unmatch "$sm_path" > /dev/null 2>&1 &&
	die "$(eval_gettext "'\$sm_path' already exists in the index")"

	if test -z "$force" && ! git add --dry-run --ignore-missing "$sm_path" > /dev/null 2>&1
	then
		cat >&2 <<EOF
The following path is ignored by one of your .gitignore files:
$(eval_gettextln $sm_path)
Use -f if you really want to add it.
EOF
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
		if test -d "$sm_path"/.git -o -f "$sm_path"/.git
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
				echo >&2 "$(eval_gettext "A git directory for '\$sm_name' is found locally with remote(s):")"
				GIT_DIR=".git/modules/$sm_name" GIT_WORK_TREE=. git remote -v | grep '(fetch)' | sed -e s,^,"  ", -e s,' (fetch)',, >&2
				echo >&2 "$(eval_gettext "If you want to reuse this local git directory instead of cloning again from")"
				echo >&2 "  $realrepo"
				echo >&2 "$(eval_gettext "use the '--force' option. If the local git directory is not the correct repo")"
				die "$(eval_gettext "or you are unsure what this means choose another name with the '--name' option.")"
			else
				echo "$(eval_gettext "Reactivating local git directory for submodule '\$sm_name'.")"
			fi
		fi
		module_clone "$sm_path" "$sm_name" "$realrepo" "$reference" || exit
		(
			clear_local_git_env
			cd "$sm_path" &&
			# ash fails to wordsplit ${branch:+-b "$branch"...}
			case "$branch" in
			'') git checkout -f -q ;;
			?*) git checkout -f -q -B "$branch" "origin/$branch" ;;
			esac
		) || die "$(eval_gettext "Unable to checkout submodule '\$sm_path'")"
	fi
	git config submodule."$sm_name".url "$realrepo"

	git add $force "$sm_path" ||
	die "$(eval_gettext "Failed to add submodule '\$sm_path'")"

	git config -f .gitmodules submodule."$sm_name".path "$sm_path" &&
	git config -f .gitmodules submodule."$sm_name".url "$repo" &&
	git add --force .gitmodules ||
	die "$(eval_gettext "Failed to register submodule '\$sm_path'")"
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

	module_list |
	while read mode sha1 stage sm_path
	do
		die_if_unmatched "$mode"
		if test -e "$sm_path"/.git
		then
			say "$(eval_gettext "Entering '\$prefix\$sm_path'")"
			name=$(module_name "$sm_path")
			(
				prefix="$prefix$sm_path/"
				clear_local_git_env
				# we make $path available to scripts ...
				path=$sm_path
				cd "$sm_path" &&
				eval "$@" &&
				if test -n "$recursive"
				then
					cmd_foreach "--recursive" "$@"
				fi
			) <&3 3<&- ||
			die "$(eval_gettext "Stopping at '\$sm_path'; script returned non-zero status.")"
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

	module_list "$@" |
	while read mode sha1 stage sm_path
	do
		die_if_unmatched "$mode"
		name=$(module_name "$sm_path") || exit

		# Copy url setting when it is not set yet
		if test -z "$(git config "submodule.$name.url")"
		then
			url=$(git config -f .gitmodules submodule."$name".url)
			test -z "$url" &&
			die "$(eval_gettext "No url found for submodule path '\$sm_path' in .gitmodules")"

			# Possibly a url relative to parent
			case "$url" in
			./*|../*)
				url=$(resolve_relative_url "$url") || exit
				;;
			esac
			git config submodule."$name".url "$url" ||
			die "$(eval_gettext "Failed to register url for submodule path '\$sm_path'")"

			say "$(eval_gettext "Submodule '\$name' (\$url) registered for path '\$sm_path'")"
		fi

		# Copy "update" setting when it is not set yet
		upd="$(git config -f .gitmodules submodule."$name".update)"
		test -z "$upd" ||
		test -n "$(git config submodule."$name".update)" ||
		git config submodule."$name".update "$upd" ||
		die "$(eval_gettext "Failed to register update mode for submodule path '\$sm_path'")"
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
	orig_flags=
	while test $# -ne 0
	do
		case "$1" in
		-q|--quiet)
			GIT_QUIET=1
			;;
		-i|--init)
			init=1
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
			orig_flags="$orig_flags $(git rev-parse --sq-quote "$1")"
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
		orig_flags="$orig_flags $(git rev-parse --sq-quote "$1")"
		shift
	done

	if test -n "$init"
	then
		cmd_init "--" "$@" || return
	fi

	cloned_modules=
	module_list "$@" | {
	err=
	while read mode sha1 stage sm_path
	do
		die_if_unmatched "$mode"
		if test "$stage" = U
		then
			echo >&2 "Skipping unmerged submodule $sm_path"
			continue
		fi
		name=$(module_name "$sm_path") || exit
		url=$(git config submodule."$name".url)
		if ! test -z "$update"
		then
			update_module=$update
		else
			update_module=$(git config submodule."$name".update)
		fi

		if test "$update_module" = "none"
		then
			echo "Skipping submodule '$sm_path'"
			continue
		fi

		if test -z "$url"
		then
			# Only mention uninitialized submodules when its
			# path have been specified
			test "$#" != "0" &&
			say "$(eval_gettext "Submodule path '\$sm_path' not initialized
Maybe you want to use 'update --init'?")"
			continue
		fi

		if ! test -d "$sm_path"/.git -o -f "$sm_path"/.git
		then
			module_clone "$sm_path" "$name" "$url" "$reference" || exit
			cloned_modules="$cloned_modules;$name"
			subsha1=
		else
			subsha1=$(clear_local_git_env; cd "$sm_path" &&
				git rev-parse --verify HEAD) ||
			die "$(eval_gettext "Unable to find current revision in submodule path '\$sm_path'")"
		fi

		if test "$subsha1" != "$sha1" -o -n "$force"
		then
			subforce=$force
			# If we don't already have a -f flag and the submodule has never been checked out
			if test -z "$subsha1" -a -z "$force"
			then
				subforce="-f"
			fi

			if test -z "$nofetch"
			then
				# Run fetch only if $sha1 isn't present or it
				# is not reachable from a ref.
				(clear_local_git_env; cd "$sm_path" &&
					( (rev=$(git rev-list -n 1 $sha1 --not --all 2>/dev/null) &&
					 test -z "$rev") || git-fetch)) ||
				die "$(eval_gettext "Unable to fetch in submodule path '\$sm_path'")"
			fi

			# Is this something we just cloned?
			case ";$cloned_modules;" in
			*";$name;"*)
				# then there is no local change to integrate
				update_module= ;;
			esac

			must_die_on_failure=
			case "$update_module" in
			rebase)
				command="git rebase"
				die_msg="$(eval_gettext "Unable to rebase '\$sha1' in submodule path '\$sm_path'")"
				say_msg="$(eval_gettext "Submodule path '\$sm_path': rebased into '\$sha1'")"
				must_die_on_failure=yes
				;;
			merge)
				command="git merge"
				die_msg="$(eval_gettext "Unable to merge '\$sha1' in submodule path '\$sm_path'")"
				say_msg="$(eval_gettext "Submodule path '\$sm_path': merged in '\$sha1'")"
				must_die_on_failure=yes
				;;
			*)
				command="git checkout $subforce -q"
				die_msg="$(eval_gettext "Unable to checkout '\$sha1' in submodule path '\$sm_path'")"
				say_msg="$(eval_gettext "Submodule path '\$sm_path': checked out '\$sha1'")"
				;;
			esac

			if (clear_local_git_env; cd "$sm_path" && $command "$sha1")
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
			(clear_local_git_env; cd "$sm_path" && eval cmd_update "$orig_flags")
			res=$?
			if test $res -gt 0
			then
				die_msg="$(eval_gettext "Failed to recurse into submodule path '\$sm_path'")"
				if test $res -eq 1
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

set_name_rev () {
	revname=$( (
		clear_local_git_env
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

	if rev=$(git rev-parse -q --verify --default HEAD ${1+"$1"})
	then
		head=$rev
		test $# = 0 || shift
	elif test -z "$1" -o "$1" = "HEAD"
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
	# Get modified modules cared by user
	modules=$(git $diff_cmd $cached --ignore-submodules=dirty --raw $head -- "$@" |
		sane_egrep '^:([0-7]* )?160000' |
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

	git $diff_cmd $cached --ignore-submodules=dirty --raw $head -- $modules |
	sane_egrep '^:([0-7]* )?160000' |
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
				eval_gettextln "unexpected mode \$mod_dst" >&2
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
			errmsg="$(eval_gettext "  Warn: \$name doesn't contain commit \$sha1_src")"
			;;
		,t)
			errmsg="$(eval_gettext "  Warn: \$name doesn't contain commit \$sha1_dst")"
			;;
		t,t)
			errmsg="$(eval_gettext "  Warn: \$name doesn't contain commits \$sha1_src and \$sha1_dst")"
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
				echo "* $name $sha1_abbr_src($blob)->$sha1_abbr_dst($submodule)$total_commits:"
			else
				echo "* $name $sha1_abbr_src($submodule)->$sha1_abbr_dst($blob)$total_commits:"
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
		if [ -n "$files" ]; then
			status_msg="$(gettextln "# Submodules changed but not updated:")"
		else
			status_msg="$(gettextln "# Submodule changes to be committed:")"
		fi
		status_sed=$(sed -e 's|^|# |' -e 's|^# $|#|')
		cat <<EOF
$status_msg
#
$status_sed
EOF
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

	module_list "$@" |
	while read mode sha1 stage sm_path
	do
		die_if_unmatched "$mode"
		name=$(module_name "$sm_path") || exit
		url=$(git config submodule."$name".url)
		displaypath="$prefix$sm_path"
		if test "$stage" = U
		then
			say "U$sha1 $displaypath"
			continue
		fi
		if test -z "$url" || ! test -d "$sm_path"/.git -o -f "$sm_path"/.git
		then
			say "-$sha1 $displaypath"
			continue;
		fi
		set_name_rev "$sm_path" "$sha1"
		if git diff-files --ignore-submodules=dirty --quiet -- "$sm_path"
		then
			say " $sha1 $displaypath$revname"
		else
			if test -z "$cached"
			then
				sha1=$(clear_local_git_env; cd "$sm_path" && git rev-parse --verify HEAD)
				set_name_rev "$sm_path" "$sha1"
			fi
			say "+$sha1 $displaypath$revname"
		fi

		if test -n "$recursive"
		then
			(
				prefix="$displaypath/"
				clear_local_git_env
				cd "$sm_path" &&
				eval cmd_status
			) ||
			die "$(eval_gettext "Failed to recurse into submodule path '\$sm_path'")"
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
	module_list "$@" |
	while read mode sha1 stage sm_path
	do
		die_if_unmatched "$mode"
		name=$(module_name "$sm_path")
		url=$(git config -f .gitmodules --get submodule."$name".url)

		# Possibly a url relative to parent
		case "$url" in
		./*|../*)
			# rewrite foo/bar as ../.. to find path from
			# submodule work tree to superproject work tree
			up_path="$(echo "$sm_path" | sed "s/[^/][^/]*/../g")" &&
			# guarantee a trailing /
			up_path=${up_path%/}/ &&
			# path from submodule work tree to submodule origin repo
			sub_origin_url=$(resolve_relative_url "$url" "$up_path") &&
			# path from superproject work tree to submodule origin repo
			super_config_url=$(resolve_relative_url "$url") || exit
			;;
		*)
			sub_origin_url="$url"
			super_config_url="$url"
			;;
		esac

		if git config "submodule.$name.url" >/dev/null 2>/dev/null
		then
			say "$(eval_gettext "Synchronizing submodule url for '\$prefix\$sm_path'")"
			git config submodule."$name".url "$super_config_url"

			if test -e "$sm_path"/.git
			then
			(
				clear_local_git_env
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
if test -n "$cached" && test "$command" != status -a "$command" != summary
then
	usage
fi

"cmd_$command" "$@"
