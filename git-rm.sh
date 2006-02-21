#!/bin/sh

USAGE='[-f] [-n] [-v] [--] <file>...'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

index_remove_option=--force-remove
remove_files=
show_only=
verbose=
while : ; do
  case "$1" in
    -f)
	remove_files=true
	index_remote_option=--force
	;;
    -n)
	show_only=true
	;;
    -v)
	verbose=--verbose
	;;
    --)
	shift; break
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

# This is typo-proofing. If some paths match and some do not, we want
# to do nothing.
case "$#" in
0)	;;
*)
	git-ls-files --error-unmatch -- "$@" >/dev/null || {
		echo >&2 "Maybe you misspelled it?"
		exit 1
	}
	;;
esac

files=$(
    if test -f "$GIT_DIR/info/exclude" ; then
	git-ls-files \
	    --exclude-from="$GIT_DIR/info/exclude" \
	    --exclude-per-directory=.gitignore -- "$@"
    else
	git-ls-files \
	--exclude-per-directory=.gitignore -- "$@"
    fi | sort | uniq
)

case "$show_only" in
true)
	echo $files
	;;
*)
	[[ "$remove_files" = "true" ]] && rm -- $files
	git-update-index $index_remove_option $verbose $files
	;;
esac
