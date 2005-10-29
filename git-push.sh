#!/bin/sh
. git-sh-setup || die "Not a git archive"

# Parse out parameters and then stop at remote, so that we can
# translate it using .git/branches information
has_all=
has_force=
has_exec=
remote=

while case "$#" in 0) break ;; esac
do
	case "$1" in
	--all)
		has_all=--all ;;
	--force)
		has_force=--force ;;
	--exec=*)
		has_exec="$1" ;;
	-*)
		die "Unknown parameter $1" ;;
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
--all) set x ;;
'')    set x $(get_remote_refs_for_push "$@") ;;
esac
shift

case "$remote" in
http://* | https://* | git://* | rsync://* )
	die "Cannot push to $remote" ;;
esac

set x "$remote" "$@"; shift
test "$has_all" && set x "$has_all" "$@" && shift
test "$has_force" && set x "$has_force" "$@" && shift
test "$has_exec" && set x "$has_exec" "$@" && shift

exec git-send-pack "$@"
