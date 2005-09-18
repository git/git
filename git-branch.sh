#!/bin/sh

. git-sh-setup || die "Not a git archive"

usage () {
    echo >&2 "usage: $(basename $0)"' [-d <branch>] | [<branch> [start-point]]

If no arguments, show available branches and mark current branch with a star.
If one argument, create a new branch <branchname> based off of current HEAD.
If two arguments, create a new branch <branchname> based off of <start-point>.
'
    exit 1
}

delete_branch () {
    option="$1" branch_name="$2"
    headref=$(readlink "$GIT_DIR/HEAD" | sed -e 's|^refs/heads/||')
    case ",$headref," in
    ",$branch_name,")
	die "Cannot delete the branch you are on." ;;
    ,,)
	die "What branch are you on anyway?" ;;
    esac
    branch=$(cat "$GIT_DIR/refs/heads/$branch_name") &&
	branch=$(git-rev-parse --verify "$branch^0") ||
	    die "Seriously, what branch are you talking about?"
    case "$option" in
    -D)
	;;
    *)
	mbs=$(git-merge-base -a "$branch" HEAD | tr '\012' ' ')
	case " $mbs " in
	*' '$branch' '*)
	    # the merge base of branch and HEAD contains branch --
	    # which means that the HEAD contains everything in the HEAD.
	    ;;
	*)
	    echo >&2 "The branch '$branch_name' is not a strict subset of your current HEAD.
If you are sure you want to delete it, run 'git branch -D $branch_name'."
	    exit 1
	    ;;
	esac
	;;
    esac
    rm -f "$GIT_DIR/refs/heads/$branch_name"
    echo "Deleted branch $branch_name."
    exit 0
}

while case "$#,$1" in 0,*) break ;; *,-*) ;; *) break ;; esac
do
	case "$1" in
	-d | -D)
		delete_branch "$1" "$2"
		exit
		;;
	--)
		shift
		break
		;;
	-*)
		usage
		;;
	esac
	shift
done

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

rev=$(git-rev-parse --verify "$head") || exit

[ -e "$GIT_DIR/refs/heads/$branchname" ] && die "$branchname already exists"

echo $rev > "$GIT_DIR/refs/heads/$branchname"
