#!/bin/sh

USAGE='[-l] [(-d | -D) <branchname>] | [[-f] <branchname> [<start-point>]] | -r'
LONG_USAGE='If no arguments, show available branches and mark current branch with a star.
If one argument, create a new branch <branchname> based off of current HEAD.
If two arguments, create a new branch <branchname> based off of <start-point>.'

SUBDIRECTORY_OK='Yes'
. git-sh-setup

headref=$(git-symbolic-ref HEAD | sed -e 's|^refs/heads/||')

delete_branch () {
    option="$1"
    shift
    for branch_name
    do
	case ",$headref," in
	",$branch_name,")
	    die "Cannot delete the branch you are on." ;;
	,,)
	    die "What branch are you on anyway?" ;;
	esac
	branch=$(git-show-ref --verify --hash -- "refs/heads/$branch_name") &&
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
		# which means that the HEAD contains everything in both.
		;;
	    *)
		echo >&2 "The branch '$branch_name' is not a strict subset of your current HEAD.
If you are sure you want to delete it, run 'git branch -D $branch_name'."
		exit 1
		;;
	    esac
	    ;;
	esac
	git update-ref -d "refs/heads/$branch_name" "$branch"
	echo "Deleted branch $branch_name."
    done
    exit 0
}

ls_remote_branches () {
    git-rev-parse --symbolic --all |
    sed -ne 's|^refs/\(remotes/\)|\1|p' |
    sort
}

force=
create_log=
while case "$#,$1" in 0,*) break ;; *,-*) ;; *) break ;; esac
do
	case "$1" in
	-d | -D)
		delete_branch "$@"
		exit
		;;
	-r)
		ls_remote_branches
		exit
		;;
	-f)
		force="$1"
		;;
	-l)
		create_log="yes"
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
	git-rev-parse --symbolic --branches |
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

git-check-ref-format "heads/$branchname" ||
	die "we do not like '$branchname' as a branch name."

if [ -d "$GIT_DIR/refs/heads/$branchname" ]
then
	for refdir in `cd "$GIT_DIR" && \
		find "refs/heads/$branchname" -type d | sort -r`
	do
		rmdir "$GIT_DIR/$refdir" || \
		    die "Could not delete '$refdir', there may still be a ref there."
	done
fi

prev=''
if git-show-ref --verify --quiet -- "refs/heads/$branchname"
then
	if test '' = "$force"
	then
		die "$branchname already exists."
	elif test "$branchname" = "$headref"
	then
		die "cannot force-update the current branch."
	fi
	prev=`git rev-parse --verify "refs/heads/$branchname"`
fi
if test "$create_log" = 'yes'
then
	mkdir -p $(dirname "$GIT_DIR/logs/refs/heads/$branchname")
	touch "$GIT_DIR/logs/refs/heads/$branchname"
fi
git update-ref -m "branch: Created from $head" "refs/heads/$branchname" "$rev" "$prev"
