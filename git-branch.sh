#!/bin/sh

. git-sh-setup || die "Not a git archive"

usage () {
    echo >&2 "usage: $(basename $0)"' [<branchname> [start-point]]

If no arguments, show available branches and mark current branch with a star.
If one argument, create a new branch <branchname> based off of current HEAD.
If two arguments, create a new branch <branchname> based off of <start-point>.
'
    exit 1
}

case "$#" in
0)
	headref=$(readlink "$GIT_DIR/HEAD" | sed -e 's|^refs/heads/||')
	git-rev-parse --symbolic --all |
	sed -ne 's|^refs/heads/||p' |
	sort |
	while read ref
	do
		if test "$headref" = "$ref"
		then
			pfx='*'
		else
			pfx=' '
		fi
		echo "$pfx $ref"
	done
	exit 0 ;;
1)
	head=HEAD ;;
2)
	head="$2^0" ;;
esac
branchname="$1"

case "$branchname" in
-*)
	usage;;
esac

rev=$(git-rev-parse --verify "$head") || exit

[ -e "$GIT_DIR/refs/heads/$branchname" ] && die "$branchname already exists"

echo $rev > "$GIT_DIR/refs/heads/$branchname"
