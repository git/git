#!/bin/sh
#
# Copyright (c) 2005 Linus Torvalds
#
. git-sh-setup || die "Not a git archive"

report () {
  header="#
# $1:
#   ($2)
#
"
  trailer=""
  while read oldmode mode oldsha sha status name newname
  do
    echo -n "$header"
    header=""
    trailer="#
"
    case "$status" in
    M ) echo "#	modified: $name";;
    D*) echo "#	deleted:  $name";;
    T ) echo "#	typechange: $name";;
    C*) echo "#	copied: $name -> $newname";;
    R*) echo "#	renamed: $name -> $newname";;
    A*) echo "#	new file: $name";;
    U ) echo "#	unmerged: $name";;
    esac
  done
  echo -n "$trailer"
  [ "$header" ]
}

branch=$(GIT_DIR="$GIT_DIR" git-symbolic-ref HEAD)
case "$branch" in
refs/heads/master) ;;
*)	echo "# On branch $branch" ;;
esac

git-update-index -q --unmerged --refresh || exit

if GIT_DIR="$GIT_DIR" git-rev-parse --verify HEAD >/dev/null 2>&1
then
	git-diff-index -M --cached HEAD |
	sed -e '
		s/^:// 
		h
		s/^[^\t]*//
		s/ /\\ /g
		x
		s/\t.*$//
		G
		s/\n/ /' |
	report "Updated but not checked in" "will commit"

	committable="$?"
else
	echo '#
# Initial commit
#'
	git-ls-files |
	sed -e '
		s/ /\\ /g
		s/^/o o o o A /' |
	report "Updated but not checked in" "will commit"

	committable="$?"
fi

git-diff-files |
sed -e '
	s/^:// 
	h
	s/^[^\t]*//
	s/ /\\ /g
	x
	s/\t.*$//
	G
	s/\n/ /' |
report "Changed but not updated" "use git-update-index to mark for commit"

if grep -v '^#' "$GIT_DIR/info/exclude" >/dev/null 2>&1
then
	git-ls-files --others \
	    --exclude-from="$GIT_DIR/info/exclude" \
	    --exclude-per-directory=.gitignore |
	sed -e '
	1i\
#\
# Ignored files:\
#   (use "git add" to add to commit)\
#
	s/^/#	/
	$a\
#'
fi

case "$committable" in
0)
	echo "nothing to commit"
	exit 1
esac
exit 0
