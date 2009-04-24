#!/bin/bash
#
# git-subtree.sh: split/join git repositories in subdirectories of this one
#
# Copyright (c) 2009 Avery Pennarun <apenwarr@gmail.com>
#
OPTS_SPEC="\
git subtree split <revisions> -- <subdir>
git subtree merge 

git subtree does foo and bar!
--
h,help   show the help
q        quiet
v        verbose
"
eval $(echo "$OPTS_SPEC" | git rev-parse --parseopt -- "$@" || echo exit $?)
. git-sh-setup
require_work_tree

quiet=
command=

debug()
{
	if [ -z "$quiet" ]; then
		echo "$@" >&2
	fi
}

#echo "Options: $*"

while [ $# -gt 0 ]; do
	opt="$1"
	shift
	case "$opt" in
		-q) quiet=1 ;;
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
	cachedir="$GIT_DIR/subtree-cache/$dir"
	rm -rf "$cachedir" || die "Can't delete old cachedir: $cachedir"
	mkdir -p "$cachedir" || die "Can't create new cachedir: $cachedir"
	debug "Using cachedir: $cachedir" >&2
	echo "$cachedir"
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
	if [ -e "$cachedir/$oldrev" ]; then
		die "cache for $oldrev already exists!"
	fi
	echo "$newrev" >"$cachedir/$oldrev"
}

cmd_split()
{
	debug "Splitting $dir..."
	cache_setup || exit $?
	
	git rev-list --reverse --parents $revs -- "$dir" |
	while read rev parents; do
		newparents=$(cache_get $parents)
		echo "rev: $rev / $newparents"
		
		git ls-tree $rev -- "$dir" |
		while read mode type tree name; do
			p=""
			for parent in $newparents; do
				p="$p -p $parent"
			done
			newrev=$(echo synthetic | git commit-tree $tree $p) \
				|| die "Can't create new commit for $rev / $tree"
			cache_set $rev $newrev
		done
	done
	
	exit 0
}

cmd_merge()
{
	die "merge command not implemented yet"
}

"cmd_$command"
