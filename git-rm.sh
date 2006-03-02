#!/bin/sh

USAGE='[-f] [-n] [-v] [--] <file>...'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

remove_files=
show_only=
verbose=
while : ; do
  case "$1" in
    -f)
	remove_files=true
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

if test -f "$GIT_DIR/info/exclude"
then
	git-ls-files -z \
	--exclude-from="$GIT_DIR/info/exclude" \
	--exclude-per-directory=.gitignore -- "$@"
else
	git-ls-files -z \
	--exclude-per-directory=.gitignore -- "$@"
fi |
case "$show_only,$remove_files" in
true,*)
	xargs -0 echo
	;;
*,true)
	xargs -0 sh -c "
		while [ \$# -gt 0 ]; do
			file=\$1; shift
			rm -- \"\$file\" && git-update-index --remove $verbose \"\$file\"
		done
	" inline
	;;
*)
	git-update-index --force-remove $verbose -z --stdin
	;;
esac
