#!/bin/sh

USAGE='[-p] [--max-count=<n>] [<since>..<limit>] [--pretty=<format>] [-m] [git-diff-tree options] [git-rev-list options]'
SUBDIRECTORY_OK='Yes'
. git-sh-setup

diff_tree_flags=$(git-rev-parse --sq --no-revs --flags "$@") || exit
test -z "$diff_tree_flags" &&
	diff_tree_flags=$(git-repo-config --get whatchanged.difftree)
test -z "$diff_tree_flags" &&
	diff_tree_flags='-M --abbrev'

rev_list_args=$(git-rev-parse --sq --default HEAD --revs-only "$@") &&
diff_tree_args=$(git-rev-parse --sq --no-revs --no-flags "$@") &&

eval "git-rev-list $rev_list_args" |
eval "git-diff-tree --stdin --pretty -r $diff_tree_flags $diff_tree_args" |
LESS="$LESS -S" ${PAGER:-less}
