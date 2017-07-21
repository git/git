# This is a shell library to calculate the remote repository and
# upstream branch that should be pulled by "git pull" from the current
# branch.

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
	op_prep="$3" # FIXME: op_prep is no longer used
	example="$4"
	branch_name=$(git symbolic-ref -q HEAD)
	display_branch_name="${branch_name#refs/heads/}"
	# If there's only one remote, use that in the suggestion
	remote="$(gettext "<remote>")"
	branch="$(gettext "<branch>")"
	if test $(git remote | wc -l) = 1
	then
		remote=$(git remote)
	fi

	if test -z "$branch_name"
	then
		gettextln "You are not currently on a branch."
	else
		gettextln "There is no tracking information for the current branch."
	fi
	case "$op_type" in
	rebase)
		gettextln "Please specify which branch you want to rebase against."
		;;
	merge)
		gettextln "Please specify which branch you want to merge with."
		;;
	*)
		echo >&2 "BUG: unknown operation type: $op_type"
		exit 1
		;;
	esac
	eval_gettextln "See git-\${cmd}(1) for details."
	echo
	echo "    $example"
	echo
	if test -n "$branch_name"
	then
		gettextln "If you wish to set tracking information for this branch you can do so with:"
		echo
		echo "    git branch --set-upstream-to=$remote/$branch $display_branch_name"
		echo
	fi
	exit 1
}
