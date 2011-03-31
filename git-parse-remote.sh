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
	if test -z "$branch_name"
	then
		echo "You are not currently on a branch, so I cannot use any
'branch.<branchname>.merge' in your configuration file.
Please specify which branch you want to $op_type $op_prep on the command
line and try again (e.g. '$example').
See git-${cmd}(1) for details."
	else
		echo "You asked me to $cmd without telling me which branch you
want to $op_type $op_prep, and 'branch.${branch_name#refs/heads/}.merge' in
your configuration file does not tell me, either. Please
specify which branch you want to use on the command line and
try again (e.g. '$example').
See git-${cmd}(1) for details.

If you often $op_type $op_prep the same branch, you may want to
use something like the following in your configuration file:
    [branch \"${branch_name#refs/heads/}\"]
    remote = <nickname>
    merge = <remote-ref>"
		test rebase = "$op_type" &&
		echo "    rebase = true"
		echo "
    [remote \"<nickname>\"]
    url = <url>
    fetch = <refspec>

See git-config(1) for details."
	fi
	exit 1
}
