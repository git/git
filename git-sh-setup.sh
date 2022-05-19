# This shell scriplet is meant to be included by other shell scripts
# to set up some variables pointing at the normal but directories and
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

but_broken_path_fix () {
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

# Source but-sh-i18n for gettext support.
. "$(but --exec-path)/but-sh-i18n"

die () {
	die_with_status 1 "$@"
}

die_with_status () {
	status=$1
	shift
	printf >&2 '%s\n' "$*"
	exit "$status"
}

BUT_QUIET=

say () {
	if test -z "$BUT_QUIET"
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
			but rev-parse --parseopt $parseopt_extra -- "$@" ||
		echo exit $?
	)"
else
	dashless=$(basename -- "$0" | sed -e 's/-/ /')
	usage() {
		die "$(eval_gettext "usage: \$dashless \$USAGE")"
	}

	if [ -z "$LONG_USAGE" ]
	then
		LONG_USAGE="$(eval_gettext "usage: \$dashless \$USAGE")"
	else
		LONG_USAGE="$(eval_gettext "usage: \$dashless \$USAGE

$LONG_USAGE")"
	fi

	case "$1" in
		-h)
		echo "$LONG_USAGE"
		exit
	esac
fi

# Set the name of the end-user facing command in the reflog when the
# script may update refs.  When BUT_REFLOG_ACTION is already set, this
# will not overwrite it, so that a scripted Porcelain (e.g. "but
# rebase") can set it to its own name (e.g. "rebase") and then call
# another scripted Porcelain (e.g. "but am") and a call to this
# function in the latter will keep the name of the end-user facing
# program (e.g. "rebase") in BUT_REFLOG_ACTION, ensuring whatever it
# does will be record as actions done as part of the end-user facing
# operation (e.g. "rebase").
#
# NOTE NOTE NOTE: consequently, after assigning a specific message to
# BUT_REFLOG_ACTION when calling a "but" command to record a custom
# reflog message, do not leave that custom value in BUT_REFLOG_ACTION,
# after you are done.  Other callers of "but" commands that rely on
# writing the default "program name" in reflog expect the variable to
# contain the value set by this function.
#
# To use a custom reflog message, do either one of these three:
#
# (a) use a single-shot export form:
#     BUT_REFLOG_ACTION="$BUT_REFLOG_ACTION: preparing frotz" \
#         but command-that-updates-a-ref
#
# (b) save the original away and restore:
#     SAVED_ACTION=$BUT_REFLOG_ACTION
#     BUT_REFLOG_ACTION="$BUT_REFLOG_ACTION: preparing frotz"
#     but command-that-updates-a-ref
#     BUT_REFLOG_ACITON=$SAVED_ACTION
#
# (c) assign the variable in a subshell:
#     (
#         BUT_REFLOG_ACTION="$BUT_REFLOG_ACTION: preparing frotz"
#         but command-that-updates-a-ref
#     )
set_reflog_action() {
	if [ -z "${BUT_REFLOG_ACTION:+set}" ]
	then
		BUT_REFLOG_ACTION="$*"
		export BUT_REFLOG_ACTION
	fi
}

but_editor() {
	if test -z "${BUT_EDITOR:+set}"
	then
		BUT_EDITOR="$(but var BUT_EDITOR)" || return $?
	fi

	eval "$BUT_EDITOR" '"$@"'
}

but_pager() {
	if test -t 1
	then
		BUT_PAGER=$(but var BUT_PAGER)
	else
		BUT_PAGER=cat
	fi
	for vardef in @@PAGER_ENV@@
	do
		var=${vardef%%=*}
		eval ": \"\${$vardef}\" && export $var"
	done

	eval "$BUT_PAGER" '"$@"'
}

is_bare_repository () {
	but rev-parse --is-bare-repository
}

cd_to_toplevel () {
	cdup=$(but rev-parse --show-toplevel) &&
	cd "$cdup" || {
		gettextln "Cannot chdir to \$cdup, the toplevel of the working tree" >&2
		exit 1
	}
}

require_work_tree_exists () {
	if test "z$(but rev-parse --is-bare-repository)" != zfalse
	then
		program_name=$0
		die "$(eval_gettext "fatal: \$program_name cannot be used without a working tree.")"
	fi
}

require_work_tree () {
	test "$(but rev-parse --is-inside-work-tree 2>/dev/null)" = true || {
		program_name=$0
		die "$(eval_gettext "fatal: \$program_name cannot be used without a working tree.")"
	}
}

require_clean_work_tree () {
	but rev-parse --verify HEAD >/dev/null || exit 1
	but update-index -q --ignore-submodules --refresh
	err=0

	if ! but diff-files --quiet --ignore-submodules
	then
		action=$1
		case "$action" in
		"rewrite branches")
			gettextln "Cannot rewrite branches: You have unstaged changes." >&2
			;;
		*)
			eval_gettextln "Cannot \$action: You have unstaged changes." >&2
			;;
		esac
		err=1
	fi

	if ! but diff-index --cached --quiet --ignore-submodules HEAD --
	then
		if test $err = 0
		then
			action=$1
			eval_gettextln "Cannot \$action: Your index contains uncummitted changes." >&2
		else
		    gettextln "Additionally, your index contains uncummitted changes." >&2
		fi
		err=1
	fi

	if test $err = 1
	then
		test -n "$2" && echo "$2" >&2
		exit 1
	fi
}

# Generate a sed script to parse identities from a cummit.
#
# Reads the cummit from stdin, which should be in raw format (e.g., from
# cat-file or "--pretty=raw").
#
# The first argument specifies the ident line to parse (e.g., "author"), and
# the second specifies the environment variable to put it in (e.g., "AUTHOR"
# for "BUT_AUTHOR_*"). Multiple pairs can be given to parse author and
# cummitter.
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
			s/.*/BUT_${uid}_NAME='&'/p

			g
			s/^$lid "'[^<]* <\([^>]*\)> .*$/\1/'"
			s/.*/BUT_${uid}_EMAIL='&'/p

			g
			s/^$lid "'[^<]* <[^>]*> \(.*\)$/@\1/'"
			s/.*/BUT_${uid}_DATE='&'/p
		}
		"
	done
	echo '/^$/q'
}

# Create a pick-script as above and feed it to sed. Stdout is suitable for
# feeding to eval.
parse_ident_from_cummit () {
	LANG=C LC_ALL=C sed -ne "$(pick_ident_script "$@")"
}

# Parse the author from a cummit given as an argument. Stdout is suitable for
# feeding to eval to set the usual BUT_* ident variables.
get_author_ident_from_cummit () {
	encoding=$(but config i18n.cummitencoding || echo UTF-8)
	but show -s --pretty=raw --encoding="$encoding" "$1" -- |
	parse_ident_from_cummit author AUTHOR
}

# Clear repo-local BUT_* environment variables. Useful when switching to
# another repository (e.g. when entering a submodule). See also the env
# list in but_connect()
clear_local_but_env() {
	unset $(but rev-parse --local-env-vars)
}

# Generate a virtual base file for a two-file merge. Uses but apply to
# remove lines from $1 that are not in $2, leaving only common lines.
create_virtual_base() {
	sz0=$(wc -c <"$1")
	@@DIFF@@ -u -La/"$1" -Lb/"$1" "$1" "$2" | but apply --no-add
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
	# but sees Windows-style pwd
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
# if we require to be in a but repository.
but_dir_init () {
	BUT_DIR=$(but rev-parse --but-dir) || exit
	if [ -z "$SUBDIRECTORY_OK" ]
	then
		test -z "$(but rev-parse --show-cdup)" || {
			exit=$?
			gettextln "You need to run this command from the toplevel of the working tree." >&2
			exit $exit
		}
	fi
	test -n "$BUT_DIR" && BUT_DIR=$(cd "$BUT_DIR" && pwd) || {
		gettextln "Unable to determine absolute path of but directory" >&2
		exit 1
	}
	: "${BUT_OBJECT_DIRECTORY="$(but rev-parse --but-path objects)"}"
}

if test -z "$NONBUT_OK"
then
	but_dir_init
fi

peel_cummittish () {
	case "$1" in
	:/*)
		peeltmp=$(but rev-parse --verify "$1") &&
		but rev-parse --verify "${peeltmp}^0"
		;;
	*)
		but rev-parse --verify "${1}^0"
		;;
	esac
}
