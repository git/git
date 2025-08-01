#!/bin/sh

test_description='check that diff --max-depth will limit recursion'
. ./test-lib.sh

make_dir() {
	mkdir -p "$1" &&
	echo "$2" >"$1/file"
}

make_files() {
	echo "$1" >file &&
	make_dir one "$1" &&
	make_dir one/two "$1" &&
	make_dir one/two/three "$1"
}

test_expect_success 'setup' '
	git commit --allow-empty -m empty &&
	git tag empty &&
	make_files added &&
	git add . &&
	git commit -m added &&
	make_files modified &&
	git add . &&
	git commit -m modified &&
	make_files index &&
	git add . &&
	make_files worktree
'

test_expect_success '--max-depth is disallowed with wildcard pathspecs' '
	test_must_fail git diff-tree --max-depth=0 HEAD^ HEAD -- "f*"
'

check_one() {
	type=$1; shift
	args=$1; shift
	path=$1; shift
	depth=$1; shift
	test_expect_${expect:-success} "diff-$type $args, path=$path, depth=$depth" "
		for i in $*; do echo \$i; done >expect &&
		git diff-$type --max-depth=$depth --name-only $args -- $path >actual &&
		test_cmp expect actual
	"
}

# For tree comparisons, we expect to see subtrees at the boundary
# get their own entry.
check_trees() {
	check_one tree "$*" '' 0 file one
	check_one tree "$*" '' 1 file one/file one/two
	check_one tree "$*" '' 2 file one/file one/two/file one/two/three
	check_one tree "$*" '' 3 file one/file one/two/file one/two/three/file
	check_one tree "$*" one 0 one
	check_one tree "$*" one 1 one/file one/two
	check_one tree "$*" one 2 one/file one/two/file one/two/three
	check_one tree "$*" one 3 one/file one/two/file one/two/three/file
	check_one tree "$*" one/two 0 one/two
	check_one tree "$*" one/two 1 one/two/file one/two/three
	check_one tree "$*" one/two 2 one/two/file one/two/three/file
	check_one tree "$*" one/two/three 0 one/two/three
	check_one tree "$*" one/two/three 1 one/two/three/file
}

# But for index comparisons, we do not store subtrees at all, so we do not
# expect them.
check_index() {
	check_one "$@" '' 0 file
	check_one "$@" '' 1 file one/file
	check_one "$@" '' 2 file one/file one/two/file
	check_one "$@" '' 3 file one/file one/two/file one/two/three/file
	check_one "$@" one 0
	check_one "$@" one 1 one/file
	check_one "$@" one 2 one/file one/two/file
	check_one "$@" one 3 one/file one/two/file one/two/three/file
	check_one "$@" one/two 0
	check_one "$@" one/two 1 one/two/file
	check_one "$@" one/two 2 one/two/file one/two/three/file
	check_one "$@" one/two/three 0
	check_one "$@" one/two/three 1 one/two/three/file
}

# Check as a modification...
check_trees HEAD^ HEAD
# ...and as an addition...
check_trees empty HEAD
# ...and as a deletion.
check_trees HEAD empty

# We currently only implement max-depth for trees.
expect=failure
# Check index against a tree
check_index index "--cached HEAD"
# and index against the worktree
check_index files ""
expect=

test_expect_success 'find shortest path within embedded pathspecs' '
	cat >expect <<-\EOF &&
	one/file
	one/two/file
	one/two/three/file
	EOF
	git diff-tree --max-depth=2 --name-only HEAD^ HEAD -- one one/two >actual &&
	test_cmp expect actual
'

test_done
