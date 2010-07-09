#!/bin/sh

# git-ls-remote could be called from outside a git managed repository;
# this would fail in that case and would issue an error message.
GIT_DIR=$(git rev-parse -q --git-dir) || :;

get_data_source () {
	case "$1" in
	*/*)
		echo ''
		;;
	.)
		echo self
		;;
	*)
		if test "$(git config --get "remote.$1.url")"
		then
			echo config
		elif test -f "$GIT_DIR/remotes/$1"
		then
			echo remotes
		elif test -f "$GIT_DIR/branches/$1"
		then
			echo branches
		else
			echo ''
		fi ;;
	esac
}

get_remote_url () {
	data_source=$(get_data_source "$1")
	case "$data_source" in
	'')
		echo "$1"
		;;
	self)
		echo "$1"
		;;
	config)
		git config --get "remote.$1.url"
		;;
	remotes)
		sed -ne '/^URL: */{
			s///p
			q
		}' "$GIT_DIR/remotes/$1"
		;;
	branches)
		sed -e 's/#.*//' "$GIT_DIR/branches/$1"
		;;
	*)
		die "internal error: get-remote-url $1" ;;
	esac
}

get_default_remote () {
	curr_branch=$(git symbolic-ref -q HEAD | sed -e 's|^refs/heads/||')
	origin=$(git config --get "branch.$curr_branch.remote")
	echo ${origin:-origin}
}

get_remote_merge_branch () {
	case "$#" in
	0|1)
	    origin="$1"
	    default=$(get_default_remote)
	    test -z "$origin" && origin=$default
	    curr_branch=$(git symbolic-ref -q HEAD)
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

	    [ -n "$remote" ] && echo "refs/remotes/$repo/$remote"
	esac
}
