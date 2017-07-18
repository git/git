#!/bin/sh

USAGE='[-p] [--max-count=<n>] [<since>..<limit>] [--pretty=<format>] [-m] [git-diff-tree options] [git-rev-list options]'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

diff_tree_flags=$(git-rev-parse --sq --no-revs --flags "$@") || exit
case "$0" in
*whatchanged)
	count=
	test -z "$diff_tree_flags" &&
		diff_tree_flags=$(git config --get whatchanged.difftree)
	diff_tree_default_flags='-c -M --abbrev' ;;
*show)
	count=-n1
	test -z "$diff_tree_flags" &&
		diff_tree_flags=$(git config --get show.difftree)
	diff_tree_default_flags='--cc --always' ;;
esac
test -z "$diff_tree_flags" &&
	diff_tree_flags="$diff_tree_default_flags"

rev_list_args=$(git-rev-parse --sq --default HEAD --revs-only "$@") &&
diff_tree_args=$(git-rev-parse --sq --no-revs --no-flags "$@") &&

eval "git-rev-list $count $rev_list_args" |
eval "git-diff-tree --stdin --pretty -r $diff_tree_flags $diff_tree_args" |
LESS="$LESS -S" ${PAGER:-less}
