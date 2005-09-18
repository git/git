#!/bin/sh

show_only=
verbose=
while : ; do
  case "$1" in
    -n)
	show_only=true
	verbose=true
	;;
    -v)
	verbose=true
	;;
    *)
	break
	;;
  esac
  shift
done

GIT_DIR=$(git-rev-parse --git-dir) || exit
global_exclude=
if [ -f "$GIT_DIR/info/exclude" ]; then
   global_exclude="--exclude-from=$GIT_DIR/info/exclude"
fi
for i in $(git-ls-files --others \
	$global_exclude --exclude-per-directory=.gitignore \
	"$@")
do
   [ "$verbose" ] && echo "  $i"
   [ "$show_only" ] || git-update-index --add -- "$i" || exit
done
