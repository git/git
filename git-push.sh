#!/bin/sh

USAGE='[--all] [--tags] [--force] <repository> [<refspec>...]'
. git-sh-setup

# Parse out parameters and then stop at remote, so that we can
# translate it using .git/branches information
has_all=
has_force=
has_exec=
has_thin=--thin
remote=
do_tags=

while case "$#" in 0) break ;; esac
do
	case "$1" in
	--all)
		has_all=--all ;;
	--tags)
		do_tags=yes ;;
	--force)
		has_force=--force ;;
	--exec=*)
		has_exec="$1" ;;
	--thin)
		;; # noop
	--no-thin)
		has_thin= ;;
	-*)
                usage ;;
        *)
		set x "$@"
		shift
		break ;;
	esac
	shift
done
case "$#" in
0)
	echo "Where would you want to push today?"
        usage ;;
esac

. git-parse-remote
remote=$(get_remote_url "$@")

case "$has_all" in
--all)
	set x ;;
'')
	case "$do_tags,$#" in
	yes,1)
		set x $(cd "$GIT_DIR/refs" && find tags -type f -print) ;;
	yes,*)
		set x $(cd "$GIT_DIR/refs" && find tags -type f -print) \
		    $(get_remote_refs_for_push "$@") ;;
	,*)
		set x $(get_remote_refs_for_push "$@") ;;
	esac
esac

shift ;# away the initial 'x'

# $# is now 0 if there was no explicit refspec on the command line
# and there was no default refspec to push from remotes/ file.
# we will let git-send-pack to do its "matching refs" thing.

case "$remote" in
git://*)
	die "Cannot use READ-ONLY transport to push to $remote" ;;
rsync://*)
        die "Pushing with rsync transport is deprecated" ;;
esac

set x "$remote" "$@"; shift
test "$has_all" && set x "$has_all" "$@" && shift
test "$has_force" && set x "$has_force" "$@" && shift
test "$has_exec" && set x "$has_exec" "$@" && shift
test "$has_thin" && set x "$has_thin" "$@" && shift

case "$remote" in
http://* | https://*)
	exec git-http-push "$@";;
*)
	exec git-send-pack "$@";;
esac
