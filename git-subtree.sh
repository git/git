#!/bin/bash
#
# git-subtree.sh: split/join git repositories in subdirectories of this one
#
# Copyright (C) 2009 Avery Pennarun <apenwarr@gmail.com>
#
if [ $# -eq 0 ]; then
    set -- -h
fi
OPTS_SPEC="\
git subtree add --prefix=<prefix> <commit>
git subtree split [options...] --prefix=<prefix> <commit...>
git subtree merge --prefix=<prefix> <commit>
git subtree pull  --prefix=<prefix> <repository> <refspec...>
--
h,help        show the help
q             quiet
prefix=       the name of the subdir to split out
 options for 'split'
annotate=     add a prefix to commit message of new commits
onto=         try connecting new tree to an existing one
rejoin        merge the new branch back into HEAD
ignore-joins  ignore prior --rejoin commits
"
eval $(echo "$OPTS_SPEC" | git rev-parse --parseopt -- "$@" || echo exit $?)
. git-sh-setup
require_work_tree

quiet=
command=
onto=
rejoin=
ignore_joins=
annotate=

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
		--annotate) annotate="$1"; shift ;;
		--no-annotate) annotate= ;;
		--prefix) prefix="$1"; shift ;;
		--no-prefix) prefix= ;;
		--onto) onto="$1"; shift ;;
		--no-onto) onto= ;;
		--rejoin) rejoin=1 ;;
		--no-rejoin) rejoin= ;;
		--ignore-joins) ignore_joins=1 ;;
		--no-ignore-joins) ignore_joins= ;;
		--) break ;;
	esac
done

command="$1"
shift
case "$command" in
	add|merge|pull) default= ;;
	split) default="--default HEAD" ;;
	*) die "Unknown command '$command'" ;;
esac

if [ -z "$prefix" ]; then
	die "You must provide the --prefix option."
fi
dir="$prefix"

if [ "$command" != "pull" ]; then
	revs=$(git rev-parse $default --revs-only "$@") || exit $?
	dirs="$(git rev-parse --no-revs --no-flags "$@")" || exit $?
	if [ -n "$dirs" ]; then
		die "Error: Use --prefix instead of bare filenames."
	fi
fi

debug "command: {$command}"
debug "quiet: {$quiet}"
debug "revs: {$revs}"
debug "dir: {$dir}"
debug "opts: {$*}"
debug

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

# if a commit doesn't have a parent, this might not work.  But we only want
# to remove the parent from the rev-list, and since it doesn't exist, it won't
# be there anyway, so do nothing in that case.
try_remove_previous()
{
	if git rev-parse "$1^" >/dev/null 2>&1; then
		echo "^$1^"
	fi
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
					try_remove_previous "$main"
					try_remove_previous "$sub"
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
		(echo -n "$annotate"; cat ) |
		git commit-tree "$2" $3  # reads the rest of stdin
	) || die "Can't copy commit $1"
}

add_msg()
{
	dir="$1"
	latest_old="$2"
	latest_new="$3"
	cat <<-EOF
		Add '$dir/' from commit '$latest_new'
		
		git-subtree-dir: $dir
		git-subtree-mainline: $latest_old
		git-subtree-split: $latest_new
	EOF
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

toptree_for_commit()
{
	commit="$1"
	git log -1 --pretty=format:'%T' "$commit" -- || exit $?
}

subtree_for_commit()
{
	commit="$1"
	dir="$2"
	git ls-tree "$commit" -- "$dir" |
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
		ptree=$(toptree_for_commit $1)
		if [ "$ptree" != "$tree" ]; then
			return 0   # changed
		else
			return 1   # not changed
		fi
	fi
}

copy_or_skip()
{
	rev="$1"
	tree="$2"
	newparents="$3"
	assert [ -n "$tree" ]

	identical=
	p=
	for parent in $newparents; do
		ptree=$(toptree_for_commit $parent) || exit $?
		if [ "$ptree" = "$tree" ]; then
			# an identical parent could be used in place of this rev.
			identical="$parent"
		fi
		if [ -n "$ptree" ]; then
			parentmatch="$parentmatch$parent"
			p="$p -p $parent"
		fi
	done
	
	if [ -n "$identical" -a "$parentmatch" = "$identical" ]; then
		echo $identical
	else
		copy_commit $rev $tree "$p" || exit $?
	fi
}

ensure_clean()
{
	if ! git diff-index HEAD --exit-code --quiet; then
		die "Working tree has modifications.  Cannot add."
	fi
	if ! git diff-index --cached HEAD --exit-code --quiet; then
		die "Index has modifications.  Cannot add."
	fi
}

cmd_add()
{
	if [ -e "$dir" ]; then
		die "'$dir' already exists.  Cannot add."
	fi
	ensure_clean
	
	set -- $revs
	if [ $# -ne 1 ]; then
		die "You must provide exactly one revision.  Got: '$revs'"
	fi
	rev="$1"
	
	debug "Adding $dir as '$rev'..."
	git read-tree --prefix="$dir" $rev || exit $?
	git checkout "$dir" || exit $?
	tree=$(git write-tree) || exit $?
	
	headrev=$(git rev-parse HEAD) || exit $?
	if [ -n "$headrev" -a "$headrev" != "$rev" ]; then
		headp="-p $headrev"
	else
		headp=
	fi
	commit=$(add_msg "$dir" "$headrev" "$rev" |
		 git commit-tree $tree $headp -p "$rev") || exit $?
	git reset "$commit" || exit $?
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
	
	if [ -n "$ignore_joins" ]; then
		unrevs=
	else
		unrevs="$(find_existing_splits "$dir" "$revs")"
	fi
	
	# We can't restrict rev-list to only "$dir" here, because that leaves out
	# critical information about commit parents.
	debug "git rev-list --reverse --parents $revs $unrevs"
	git rev-list --reverse --parents $revs $unrevs |
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
		
		tree=$(subtree_for_commit $rev "$dir")
		debug "  tree is: $tree"
		[ -z $tree ] && continue

		newrev=$(copy_or_skip "$rev" "$tree" "$newparents") || exit $?
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
	ensure_clean
	
	set -- $revs
	if [ $# -ne 1 ]; then
		die "You must provide exactly one revision.  Got: '$revs'"
	fi
	rev="$1"
	
	git merge -s subtree $rev
}

cmd_pull()
{
	ensure_clean
	set -x
	git pull -s subtree "$@"
}

"cmd_$command" "$@"
