#!/bin/sh
. git-sh-setup || die "Not a git archive"

old=$(git-rev-parse HEAD)
new=
force=
branch=
newbranch=
while [ "$#" != "0" ]; do
    arg="$1"
    shift
    case "$arg" in
	"-b")
		newbranch="$1"
		shift
		[ -z "$newbranch" ] &&
			die "git checkout: -b needs a branch name"
		[ -e "$GIT_DIR/refs/heads/$newbranch" ] &&
			die "git checkout: branch $newbranch already exists"
		git-check-ref-format "heads/$newbranch" ||
			die "we do not like '$newbranch' as a branch name."
		;;
	"-f")
		force=1
		;;
	*)
		rev=$(git-rev-parse --verify "$arg^0" 2>/dev/null) ||
			die "I don't know any '$arg'."
		if [ -z "$rev" ]; then
			echo "unknown flag $arg"
			exit 1
		fi
		if [ "$new" ]; then
			echo "Multiple revisions?"
			exit 1
		fi
		new="$rev"
		if [ -f "$GIT_DIR/refs/heads/$arg" ]; then
			branch="$arg"
		fi
		;;
    esac
done
[ -z "$new" ] && new=$old

#
# If we don't have an old branch that we're switching to,
# and we don't have a new branch name for the target we
# are switching to, then we'd better just be checking out
# what we already had
#
[ -z "$branch$newbranch" ] &&
	[ "$new" != "$old" ] &&
	die "git checkout: you need to specify a new branch name"

if [ "$force" ]
then
    git-read-tree --reset $new &&
	git-checkout-index -q -f -u -a
else
    git-update-index --refresh >/dev/null
    git-read-tree -m -u $old $new
fi

# 
# Switch the HEAD pointer to the new branch if it we
# checked out a branch head, and remove any potential
# old MERGE_HEAD's (subsequent commits will clearly not
# be based on them, since we re-set the index)
#
if [ "$?" -eq 0 ]; then
	if [ "$newbranch" ]; then
		echo $new > "$GIT_DIR/refs/heads/$newbranch"
		branch="$newbranch"
	fi
	[ "$branch" ] &&
	GIT_DIR="$GIT_DIR" git-symbolic-ref HEAD "refs/heads/$branch"
	rm -f "$GIT_DIR/MERGE_HEAD"
else
	exit 1
fi
