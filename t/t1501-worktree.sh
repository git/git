#!/bin/sh

test_description='test separate work tree'
. ./test-lib.sh

test_rev_parse() {
	name=$1
	shift

	test_expect_success "$name: is-bare-repository" \
	"test '$1' = \"\$(git rev-parse --is-bare-repository)\""
	shift
	[ $# -eq 0 ] && return

	test_expect_success "$name: is-inside-git-dir" \
	"test '$1' = \"\$(git rev-parse --is-inside-git-dir)\""
	shift
	[ $# -eq 0 ] && return

	test_expect_success "$name: is-inside-work-tree" \
	"test '$1' = \"\$(git rev-parse --is-inside-work-tree)\""
	shift
	[ $# -eq 0 ] && return

	test_expect_success "$name: prefix" \
	"test '$1' = \"\$(git rev-parse --show-prefix)\""
	shift
	[ $# -eq 0 ] && return
}

mkdir -p work/sub/dir || exit 1
mv .git repo.git || exit 1

say "core.worktree = relative path"
export GIT_DIR=repo.git
export GIT_CONFIG="$(pwd)"/$GIT_DIR/config
unset GIT_WORK_TREE
git config core.worktree ../work
test_rev_parse 'outside'      false false false
cd work || exit 1
export GIT_DIR=../repo.git
export GIT_CONFIG="$(pwd)"/$GIT_DIR/config
test_rev_parse 'inside'       false false true ''
cd sub/dir || exit 1
export GIT_DIR=../../../repo.git
export GIT_CONFIG="$(pwd)"/$GIT_DIR/config
test_rev_parse 'subdirectory' false false true sub/dir/
cd ../../.. || exit 1

say "core.worktree = absolute path"
export GIT_DIR=$(pwd)/repo.git
export GIT_CONFIG=$GIT_DIR/config
git config core.worktree "$(pwd)/work"
test_rev_parse 'outside'      false false false
cd work || exit 1
test_rev_parse 'inside'       false false true ''
cd sub/dir || exit 1
test_rev_parse 'subdirectory' false false true sub/dir/
cd ../../.. || exit 1

say "GIT_WORK_TREE=relative path (override core.worktree)"
export GIT_DIR=$(pwd)/repo.git
export GIT_CONFIG=$GIT_DIR/config
git config core.worktree non-existent
export GIT_WORK_TREE=work
test_rev_parse 'outside'      false false false
cd work || exit 1
export GIT_WORK_TREE=.
test_rev_parse 'inside'       false false true ''
cd sub/dir || exit 1
export GIT_WORK_TREE=../..
test_rev_parse 'subdirectory' false false true sub/dir/
cd ../../.. || exit 1

mv work repo.git/work

say "GIT_WORK_TREE=absolute path, work tree below git dir"
export GIT_DIR=$(pwd)/repo.git
export GIT_CONFIG=$GIT_DIR/config
export GIT_WORK_TREE=$(pwd)/repo.git/work
test_rev_parse 'outside'              false false false
cd repo.git || exit 1
test_rev_parse 'in repo.git'              false true  false
cd objects || exit 1
test_rev_parse 'in repo.git/objects'      false true  false
cd ../work || exit 1
test_rev_parse 'in repo.git/work'         false true true ''
cd sub/dir || exit 1
test_rev_parse 'in repo.git/sub/dir' false true true sub/dir/
cd ../../../.. || exit 1

test_expect_success 'repo finds its work tree' '
	(cd repo.git &&
	 : > work/sub/dir/untracked &&
	 test sub/dir/untracked = "$(git ls-files --others)")
'

test_expect_success 'repo finds its work tree from work tree, too' '
	(cd repo.git/work/sub/dir &&
	 : > tracked &&
	 git --git-dir=../../.. add tracked &&
	 cd ../../.. &&
	 test sub/dir/tracked = "$(git ls-files)")
'

test_done
