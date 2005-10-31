#!/bin/sh

usage() {
    die "usage: git add [-n] [-v] <file>..."
}

show_only=
verbose=
while : ; do
  case "$1" in
    -n)
	show_only=true
	;;
    -v)
	verbose=--verbose
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

GIT_DIR=$(git-rev-parse --git-dir) || exit

if test -f "$GIT_DIR/info/exclude"
then
	git-ls-files -z \
	--exclude-from="$GIT_DIR/info/exclude" \
	--others --exclude-per-directory=.gitignore -- "$@"
else
	git-ls-files -z \
	--others --exclude-per-directory=.gitignore -- "$@"
fi |
case "$show_only" in
true)
	xargs -0 echo ;;
*)
	git-update-index --add $verbose -z --stdin ;;
esac
