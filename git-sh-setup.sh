# This shell scriplet is meant to be included by other shell scripts
# to set up some variables pointing at the normal git directories and
# a few helper shell functions.

# Having this variable in your environment would break scripts because
# you would cause "cd" to be taken to unexpected places.  If you
# like CDPATH, define it for your interactive shell sessions without
# exporting it.
# But we protect ourselves from such a user mistake nevertheless.
unset CDPATH

# Similarly for IFS, but some shells (e.g. FreeBSD 7.2) are buggy and
# do not equate an unset IFS with IFS with the default, so here is
# an explicit SP HT LF.
IFS=' 	
'

git_broken_path_fix () {
	case ":$PATH:" in
	*:$1:*) : ok ;;
	*)
		PATH=$(
			SANE_TOOL_PATH="$1"
			IFS=: path= sep=
			set x $PATH
			shift
			for elem
			do
				case "$SANE_TOOL_PATH:$elem" in
				(?*:/bin | ?*:/usr/bin)
					path="$path$sep$SANE_TOOL_PATH"
					sep=:
					SANE_TOOL_PATH=
				esac
				path="$path$sep$elem"
				sep=:
			done
			echo "$path"
		)
		;;
	esac
}

# @@BROKEN_PATH_FIX@@

die () {
	die_with_status 1 "$@"
}

die_with_status () {
	status=$1
	shift
	printf >&2 '%s\n' "$*"
	exit "$status"
}

GIT_QUIET=

say () {
	if test -z "$GIT_QUIET"
	then
		printf '%s\n' "$*"
	fi
}

if test -n "$OPTIONS_SPEC"; then
	usage() {
		"$0" -h
		exit 1
	}

	parseopt_extra=
	[ -n "$OPTIONS_KEEPDASHDASH" ] &&
		parseopt_extra="--keep-dashdash"
	[ -n "$OPTIONS_STUCKLONG" ] &&
		parseopt_extra="$parseopt_extra --stuck-long"

	eval "$(
		echo "$OPTIONS_SPEC" |
			git rev-parse --parseopt $parseopt_extra -- "$@" ||
		echo exit $?
	)"
else
	dashless=$(basename "$0" | sed -e 's/-/ /')
	usage() {
		die "usage: $dashless $USAGE"
	}

	if [ -z "$LONG_USAGE" ]
	then
		LONG_USAGE="usage: $dashless $USAGE"
	else
		LONG_USAGE="usage: $dashless $USAGE

$LONG_USAGE"
	fi

	case "$1" in
		-h)
		echo "$LONG_USAGE"
		exit
	esac
fi

# Set the name of the end-user facing command in the reflog when the
# script may update refs.  When GIT_REFLOG_ACTION is already set, this
# will not overwrite it, so that a scripted Porcelain (e.g. "git
# rebase") can set it to its own name (e.g. "rebase") and then call
# another scripted Porcelain (e.g. "git am") and a call to this
# function in the latter will keep the name of the end-user facing
# program (e.g. "rebase") in GIT_REFLOG_ACTION, ensuring whatever it
# does will be record as actions done as part of the end-user facing
# operation (e.g. "rebase").
#
# NOTE NOTE NOTE: consequently, after assigning a specific message to
# GIT_REFLOG_ACTION when calling a "git" command to record a custom
# reflog message, do not leave that custom value in GIT_REFLOG_ACTION,
# after you are done.  Other callers of "git" commands that rely on
# writing the default "program name" in reflog expect the variable to
# contain the value set by this function.
#
# To use a custom reflog message, do either one of these three:
#
# (a) use a single-shot export form:
#     GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION: preparing frotz" \
#         git command-that-updates-a-ref
#
# (b) save the original away and restore:
#     SAVED_ACTION=$GIT_REFLOG_ACTION
#     GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION: preparing frotz"
#     git command-that-updates-a-ref
#     GIT_REFLOG_ACITON=$SAVED_ACTION
#
# (c) assign the variable in a subshell:
#     (
#         GIT_REFLOG_ACTION="$GIT_REFLOG_ACTION: preparing frotz"
#         git command-that-updates-a-ref
#     )
set_reflog_action() {
	if [ -z "${GIT_REFLOG_ACTION:+set}" ]
	then
		GIT_REFLOG_ACTION="$*"
		export GIT_REFLOG_ACTION
	fi
}

git_editor() {
	if test -z "${GIT_EDITOR:+set}"
	then
		GIT_EDITOR="$(git var GIT_EDITOR)" || return $?
	fi

	eval "$GIT_EDITOR" '"$@"'
}

git_pager() {
	if test -t 1
	then
		GIT_PAGER=$(git var GIT_PAGER)
	else
		GIT_PAGER=cat
	fi
	: ${LESS=-FRX}
	: ${LV=-c}
	export LESS LV

	eval "$GIT_PAGER" '"$@"'
}

sane_grep () {
	GREP_OPTIONS= LC_ALL=C grep "$@"
}

sane_egrep () {
	GREP_OPTIONS= LC_ALL=C egrep "$@"
}

is_bare_repository () {
	git rev-parse --is-bare-repository
}

cd_to_toplevel () {
	cdup=$(git rev-parse --show-toplevel) &&
	cd "$cdup" || {
		echo >&2 "Cannot chdir to $cdup, the toplevel of the working tree"
		exit 1
	}
}

require_work_tree_exists () {
	if test "z$(git rev-parse --is-bare-repository)" != zfalse
	then
		die "fatal: $0 cannot be used without a working tree."
	fi
}

require_work_tree () {
	test "$(git rev-parse --is-inside-work-tree 2>/dev/null)" = true ||
	die "fatal: $0 cannot be used without a working tree."
}

require_clean_work_tree () {
	git rev-parse --verify HEAD >/dev/null || exit 1
	git update-index -q --ignore-submodules --refresh
	err=0

	if ! git diff-files --quiet --ignore-submodules
	then
		echo >&2 "Cannot $1: You have unstaged changes."
		err=1
	fi

	if ! git diff-index --cached --quiet --ignore-submodules HEAD --
	then
		if [ $err = 0 ]
		then
		    echo >&2 "Cannot $1: Your index contains uncommitted changes."
		else
		    echo >&2 "Additionally, your index contains uncommitted changes."
		fi
		err=1
	fi

	if [ $err = 1 ]
	then
		test -n "$2" && echo >&2 "$2"
		exit 1
	fi
}

# Generate a sed script to parse identities from a commit.
#
# Reads the commit from stdin, which should be in raw format (e.g., from
# cat-file or "--pretty=raw").
#
# The first argument specifies the ident line to parse (e.g., "author"), and
# the second specifies the environment variable to put it in (e.g., "AUTHOR"
# for "GIT_AUTHOR_*"). Multiple pairs can be given to parse author and
# committer.
pick_ident_script () {
	while test $# -gt 0
	do
		lid=$1; shift
		uid=$1; shift
		printf '%s' "
		/^$lid /{
			s/'/'\\\\''/g
			h
			s/^$lid "'\([^<]*\) <[^>]*> .*$/\1/'"
			s/.*/GIT_${uid}_NAME='&'/p

			g
			s/^$lid "'[^<]* <\([^>]*\)> .*$/\1/'"
			s/.*/GIT_${uid}_EMAIL='&'/p

			g
			s/^$lid "'[^<]* <[^>]*> \(.*\)$/@\1/'"
			s/.*/GIT_${uid}_DATE='&'/p
		}
		"
	done
	echo '/^$/q'
}

# Create a pick-script as above and feed it to sed. Stdout is suitable for
# feeding to eval.
parse_ident_from_commit () {
	LANG=C LC_ALL=C sed -ne "$(pick_ident_script "$@")"
}

# Parse the author from a commit given as an argument. Stdout is suitable for
# feeding to eval to set the usual GIT_* ident variables.
get_author_ident_from_commit () {
	encoding=$(git config i18n.commitencoding || echo UTF-8)
	git show -s --pretty=raw --encoding="$encoding" "$1" -- |
	parse_ident_from_commit author AUTHOR
}

# Clear repo-local GIT_* environment variables. Useful when switching to
# another repository (e.g. when entering a submodule). See also the env
# list in git_connect()
clear_local_git_env() {
	unset $(git rev-parse --local-env-vars)
}

# Generate a virtual base file for a two-file merge. Uses git apply to
# remove lines from $1 that are not in $2, leaving only common lines.
create_virtual_base() {
	sz0=$(wc -c <"$1")
	@@DIFF@@ -u -La/"$1" -Lb/"$1" "$1" "$2" | git apply --no-add
	sz1=$(wc -c <"$1")

	# If we do not have enough common material, it is not
	# worth trying two-file merge using common subsections.
	expr $sz0 \< $sz1 \* 2 >/dev/null || : >"$1"
}


# Platform specific tweaks to work around some commands
case $(uname -s) in
*MINGW*)
	# Windows has its own (incompatible) sort and find
	sort () {
		/usr/bin/sort "$@"
	}
	find () {
		/usr/bin/find "$@"
	}
	# git sees Windows-style pwd
	pwd () {
		builtin pwd -W
	}
	is_absolute_path () {
		case "$1" in
		[/\\]* | [A-Za-z]:*)
			return 0 ;;
		esac
		return 1
	}
	;;
*)
	is_absolute_path () {
		case "$1" in
		/*)
			return 0 ;;
		esac
		return 1
	}
esac

# Make sure we are in a valid repository of a vintage we understand,
# if we require to be in a git repository.
if test -z "$NONGIT_OK"
then
	GIT_DIR=$(git rev-parse --git-dir) || exit
	if [ -z "$SUBDIRECTORY_OK" ]
	then
		test -z "$(git rev-parse --show-cdup)" || {
			exit=$?
			echo >&2 "You need to run this command from the toplevel of the working tree."
			exit $exit
		}
	fi
	test -n "$GIT_DIR" && GIT_DIR=$(cd "$GIT_DIR" && pwd) || {
		echo >&2 "Unable to determine absolute path of git directory"
		exit 1
	}
	: ${GIT_OBJECT_DIRECTORY="$GIT_DIR/objects"}
fi

peel_committish () {
	case "$1" in
	:/*)
		peeltmp=$(git rev-parse --verify "$1") &&
		git rev-parse --verify "${peeltmp}^0"
		;;
	*)
		git rev-parse --verify "${1}^0"
		;;
	esac
}
