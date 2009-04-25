#!/bin/bash
#
# git-subtree.sh: split/join git repositories in subdirectories of this one
#
# Copyright (C) 2009 Avery Pennarun <apenwarr@gmail.com>
#
OPTS_SPEC="\
git subtree split [--rejoin] [--onto rev] <commit...> -- <path>
git subtree merge 

git subtree does foo and bar!
--
h,help   show the help
q        quiet
v        verbose
onto=    existing subtree revision to search for parent
rejoin   merge the new branch back into HEAD
"
eval $(echo "$OPTS_SPEC" | git rev-parse --parseopt -- "$@" || echo exit $?)
. git-sh-setup
require_work_tree

quiet=
command=
onto=
rejoin=

debug()
{
	if [ -z "$quiet" ]; then
		echo "$@" >&2
	fi
}

assert()
{
	if "$@"; then
		:
	else
		die "assertion failed: " "$@"
	fi
}


#echo "Options: $*"

while [ $# -gt 0 ]; do
	opt="$1"
	shift
	case "$opt" in
		-q) quiet=1 ;;
		--onto) onto="$1"; shift ;;
		--rejoin) rejoin=1 ;;
		--) break ;;
	esac
done

command="$1"
shift
case "$command" in
	split|merge) ;;
	*) die "Unknown command '$command'" ;;
esac

revs=$(git rev-parse --default HEAD --revs-only "$@") || exit $?
dirs="$(git rev-parse --sq --no-revs --no-flags "$@")" || exit $?

#echo "dirs is {$dirs}"
eval $(echo set -- $dirs)
if [ "$#" -ne 1 ]; then
	die "Must provide exactly one subtree dir (got $#)"
fi
dir="$1"

debug "command: {$command}"
debug "quiet: {$quiet}"
debug "revs: {$revs}"
debug "dir: {$dir}"

cache_setup()
{
	cachedir="$GIT_DIR/subtree-cache/$$"
	rm -rf "$cachedir" || die "Can't delete old cachedir: $cachedir"
	mkdir -p "$cachedir" || die "Can't create new cachedir: $cachedir"
	debug "Using cachedir: $cachedir" >&2
}

cache_get()
{
	for oldrev in $*; do
		if [ -r "$cachedir/$oldrev" ]; then
			read newrev <"$cachedir/$oldrev"
			echo $newrev
		fi
	done
}

cache_set()
{
	oldrev="$1"
	newrev="$2"
	if [ "$oldrev" != "latest_old" \
	     -a "$oldrev" != "latest_new" \
	     -a -e "$cachedir/$oldrev" ]; then
		die "cache for $oldrev already exists!"
	fi
	echo "$newrev" >"$cachedir/$oldrev"
}

find_existing_splits()
{
	debug "Looking for prior splits..."
	dir="$1"
	revs="$2"
	git log --grep="^git-subtree-dir: $dir\$" \
		--pretty=format:'%s%n%n%b%nEND' "$revs" |
	while read a b junk; do
		case "$a" in
			git-subtree-mainline:) main="$b" ;;
			git-subtree-split:) sub="$b" ;;
			*)
				if [ -n "$main" -a -n "$sub" ]; then
					debug "  Prior: $main -> $sub"
					cache_set $main $sub
					echo "^$main^ ^$sub^"
					main=
					sub=
				fi
				;;
		esac
	done
}

copy_commit()
{
	# We're doing to set some environment vars here, so
	# do it in a subshell to get rid of them safely later
	git log -1 --pretty=format:'%an%n%ae%n%ad%n%cn%n%ce%n%cd%n%s%n%n%b' "$1" |
	(
		read GIT_AUTHOR_NAME
		read GIT_AUTHOR_EMAIL
		read GIT_AUTHOR_DATE
		read GIT_COMMITTER_NAME
		read GIT_COMMITTER_EMAIL
		read GIT_COMMITTER_DATE
		export  GIT_AUTHOR_NAME \
			GIT_AUTHOR_EMAIL \
			GIT_AUTHOR_DATE \
			GIT_COMMITTER_NAME \
			GIT_COMMITTER_EMAIL \
			GIT_COMMITTER_DATE
		(echo -n '*'; cat ) |  # FIXME
		git commit-tree "$2" $3  # reads the rest of stdin
	) || die "Can't copy commit $1"
}

merge_msg()
{
	dir="$1"
	latest_old="$2"
	latest_new="$3"
	cat <<-EOF
		Split '$dir/' into commit '$latest_new'
		
		git-subtree-dir: $dir
		git-subtree-mainline: $latest_old
		git-subtree-split: $latest_new
	EOF
}

tree_for_commit()
{
	git ls-tree "$1" -- "$dir" |
	while read mode type tree name; do
		assert [ "$name" = "$dir" ]
		echo $tree
		break
	done
}

tree_changed()
{
	tree=$1
	shift
	if [ $# -ne 1 ]; then
		return 0   # weird parents, consider it changed
	else
		ptree=$(tree_for_commit $1)
		if [ "$ptree" != "$tree" ]; then
			return 0   # changed
		else
			return 1   # not changed
		fi
	fi
}

cmd_split()
{
	debug "Splitting $dir..."
	cache_setup || exit $?
	
	if [ -n "$onto" ]; then
		debug "Reading history for --onto=$onto..."
		git rev-list $onto |
		while read rev; do
			# the 'onto' history is already just the subdir, so
			# any parent we find there can be used verbatim
			debug "  cache: $rev"
			cache_set $rev $rev
		done
	fi
	
	unrevs="$(find_existing_splits "$dir" "$revs")"
	
	debug "git rev-list --reverse $revs $unrevs"
	git rev-list --reverse --parents $revs $unrevsx |
	while read rev parents; do
		debug
		debug "Processing commit: $rev"
		exists=$(cache_get $rev)
		if [ -n "$exists" ]; then
			debug "  prior: $exists"
			continue
		fi
		debug "  parents: $parents"
		newparents=$(cache_get $parents)
		debug "  newparents: $newparents"
		
		tree=$(tree_for_commit $rev)
		debug "  tree is: $tree"
		[ -z $tree ] && continue

		p=""
		for parent in $newparents; do
			p="$p -p $parent"
		done
			
		if tree_changed $tree $parents; then
			newrev=$(copy_commit $rev $tree "$p") || exit $?
		else
			newrev="$newparents"
		fi
		debug "  newrev is: $newrev"
		cache_set $rev $newrev
		cache_set latest_new $newrev
		cache_set latest_old $rev
	done || exit $?
	latest_new=$(cache_get latest_new)
	if [ -z "$latest_new" ]; then
		die "No new revisions were found"
	fi
	
	if [ -n "$rejoin" ]; then
		debug "Merging split branch into HEAD..."
		latest_old=$(cache_get latest_old)
		git merge -s ours \
			-m "$(merge_msg $dir $latest_old $latest_new)" \
			$latest_new >&2
	fi
	echo $latest_new
	exit 0
}

cmd_merge()
{
	die "merge command not implemented yet"
}

"cmd_$command"
