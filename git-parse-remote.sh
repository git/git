#!/bin/sh

# git-ls-remote could be called from outside a git managed repository;
# this would fail in that case and would issue an error message.
GIT_DIR=$(git rev-parse -q --git-dir) || :;

get_default_remote () {
	curr_branch=$(git symbolic-ref -q HEAD)
	curr_branch="${curr_branch#refs/heads/}"
	origin=$(git config --get "branch.$curr_branch.remote")
	echo ${origin:-origin}
}

get_remote_merge_branch () {
	case "$#" in
	0|1)
	    origin="$1"
	    default=$(get_default_remote)
	    test -z "$origin" && origin=$default
	    curr_branch=$(git symbolic-ref -q HEAD) &&
	    [ "$origin" = "$default" ] &&
	    echo $(git for-each-ref --format='%(upstream)' $curr_branch)
	    ;;
	*)
	    repo=$1
	    shift
	    ref=$1
	    # FIXME: It should return the tracking branch
	    #        Currently only works with the default mapping
	    case "$ref" in
	    +*)
		ref=$(expr "z$ref" : 'z+\(.*\)')
		;;
	    esac
	    expr "z$ref" : 'z.*:' >/dev/null || ref="${ref}:"
	    remote=$(expr "z$ref" : 'z\([^:]*\):')
	    case "$remote" in
	    '' | HEAD ) remote=HEAD ;;
	    heads/*) remote=${remote#heads/} ;;
	    refs/heads/*) remote=${remote#refs/heads/} ;;
	    refs/* | tags/* | remotes/* ) remote=
	    esac
	    [ -n "$remote" ] && case "$repo" in
		.)
		    echo "refs/heads/$remote"
		    ;;
		*)
		    echo "refs/remotes/$repo/$remote"
		    ;;
	    esac
	esac
}

error_on_missing_default_upstream () {
	cmd="$1"
	op_type="$2"
	op_prep="$3"
	example="$4"
	branch_name=$(git symbolic-ref -q HEAD)
	# If there's only one remote, use that in the suggestion
	remote="<remote>"
	if test $(git remote | wc -l) = 1
	then
		remote=$(git remote)
	fi

	if test -z "$branch_name"
	then
		echo "You are not currently on a branch. Please specify which
branch you want to $op_type $op_prep. See git-${cmd}(1) for details.

    $example
"
	else
		echo "There is no tracking information for the current branch.
Please specify which branch you want to $op_type $op_prep.
See git-${cmd}(1) for details

    $example

If you wish to set tracking information for this branch you can do so with:

    git branch --set-upstream-to=$remote/<branch> ${branch_name#refs/heads/}
"
	fi
	exit 1
}
