#!/bin/sh

USAGE='[-f] [-b <new_branch>] [-m] [<branch>] [<paths>...]'
SUBDIRECTORY_OK=Sometimes
. git-sh-setup

old=$(git-rev-parse HEAD)
old_name=HEAD
new=
new_name=
force=
branch=
newbranch=
newbranch_log=
merge=
while [ "$#" != "0" ]; do
    arg="$1"
    shift
    case "$arg" in
	"-b")
		newbranch="$1"
		shift
		[ -z "$newbranch" ] &&
			die "git checkout: -b needs a branch name"
		git-show-ref --verify --quiet -- "refs/heads/$newbranch" &&
			die "git checkout: branch $newbranch already exists"
		git-check-ref-format "heads/$newbranch" ||
			die "git checkout: we do not like '$newbranch' as a branch name."
		;;
	"-l")
		newbranch_log=1
		;;
	"-f")
		force=1
		;;
	-m)
		merge=1
		;;
	--)
		break
		;;
	-*)
		usage
		;;
	*)
		if rev=$(git-rev-parse --verify "$arg^0" 2>/dev/null)
		then
			if [ -z "$rev" ]; then
				echo "unknown flag $arg"
				exit 1
			fi
			new="$rev"
			new_name="$arg^0"
			if git-show-ref --verify --quiet -- "refs/heads/$arg"
			then
				branch="$arg"
			fi
		elif rev=$(git-rev-parse --verify "$arg^{tree}" 2>/dev/null)
		then
			# checking out selected paths from a tree-ish.
			new="$rev"
			new_name="$arg^{tree}"
			branch=
		else
			new=
			new_name=
			branch=
			set x "$arg" "$@"
			shift
		fi
		case "$1" in
		--)
			shift ;;
		esac
		break
		;;
    esac
done

# The behaviour of the command with and without explicit path
# parameters is quite different.
#
# Without paths, we are checking out everything in the work tree,
# possibly switching branches.  This is the traditional behaviour.
#
# With paths, we are _never_ switching branch, but checking out
# the named paths from either index (when no rev is given),
# or the named tree-ish (when rev is given).

if test "$#" -ge 1
then
	hint=
	if test "$#" -eq 1
	then
		hint="
Did you intend to checkout '$@' which can not be resolved as commit?"
	fi
	if test '' != "$newbranch$force$merge"
	then
		die "git checkout: updating paths is incompatible with switching branches/forcing$hint"
	fi
	if test '' != "$new"
	then
		# from a specific tree-ish; note that this is for
		# rescuing paths and is never meant to remove what
		# is not in the named tree-ish.
		git-ls-tree --full-name -r "$new" "$@" |
		git-update-index --index-info || exit $?
	fi
	git-checkout-index -f -u -- "$@"
	exit $?
else
	# Make sure we did not fall back on $arg^{tree} codepath
	# since we are not checking out from an arbitrary tree-ish,
	# but switching branches.
	if test '' != "$new"
	then
		git-rev-parse --verify "$new^{commit}" >/dev/null 2>&1 ||
		die "Cannot switch branch to a non-commit."
	fi
fi

# We are switching branches and checking out trees, so
# we *NEED* to be at the toplevel.
cdup=$(git-rev-parse --show-cdup)
if test ! -z "$cdup"
then
	cd "$cdup"
fi

[ -z "$new" ] && new=$old && new_name="$old_name"

# If we don't have an old branch that we're switching to,
# and we don't have a new branch name for the target we
# are switching to, then we'd better just be checking out
# what we already had

[ -z "$branch$newbranch" ] &&
	[ "$new" != "$old" ] &&
	die "git checkout: to checkout the requested commit you need to specify 
              a name for a new branch which is created and switched to"

if [ "$force" ]
then
    git-read-tree --reset -u $new
else
    git-update-index --refresh >/dev/null
    merge_error=$(git-read-tree -m -u $old $new 2>&1) || (
	case "$merge" in
	'')
		echo >&2 "$merge_error"
		exit 1 ;;
	esac

	# Match the index to the working tree, and do a three-way.
    	git diff-files --name-only | git update-index --remove --stdin &&
	work=`git write-tree` &&
	git read-tree --reset -u $new &&
	git read-tree -m -u --aggressive $old $new $work || exit

	if result=`git write-tree 2>/dev/null`
	then
	    echo >&2 "Trivially automerged."
	else
	    git merge-index -o git-merge-one-file -a
	fi

	# Do not register the cleanly merged paths in the index yet.
	# this is not a real merge before committing, but just carrying
	# the working tree changes along.
	unmerged=`git ls-files -u`
	git read-tree --reset $new
	case "$unmerged" in
	'')	;;
	*)
		(
			z40=0000000000000000000000000000000000000000
			echo "$unmerged" |
			sed -e 's/^[0-7]* [0-9a-f]* /'"0 $z40 /"
			echo "$unmerged"
		) | git update-index --index-info
		;;
	esac
	exit 0
    )
    saved_err=$?
    if test "$saved_err" = 0
    then
	test "$new" = "$old" || git diff-index --name-status "$new"
    fi
    (exit $saved_err)
fi

# 
# Switch the HEAD pointer to the new branch if we
# checked out a branch head, and remove any potential
# old MERGE_HEAD's (subsequent commits will clearly not
# be based on them, since we re-set the index)
#
if [ "$?" -eq 0 ]; then
	if [ "$newbranch" ]; then
		if [ "$newbranch_log" ]; then
			mkdir -p $(dirname "$GIT_DIR/logs/refs/heads/$newbranch")
			touch "$GIT_DIR/logs/refs/heads/$newbranch"
		fi
		git-update-ref -m "checkout: Created from $new_name" "refs/heads/$newbranch" $new || exit
		branch="$newbranch"
	fi
	[ "$branch" ] &&
	GIT_DIR="$GIT_DIR" git-symbolic-ref HEAD "refs/heads/$branch"
	rm -f "$GIT_DIR/MERGE_HEAD"
else
	exit 1
fi
