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

EMPTY_TREE=$(git write-tree)
mkdir -p work/sub/dir || exit 1
mkdir -p work2 || exit 1
mv .git repo.git || exit 1

say "core.worktree = relative path"
GIT_DIR=repo.git
GIT_CONFIG="$(pwd)"/$GIT_DIR/config
export GIT_DIR GIT_CONFIG
unset GIT_WORK_TREE
git config core.worktree ../work
test_rev_parse 'outside'      false false false
cd work || exit 1
GIT_DIR=../repo.git
GIT_CONFIG="$(pwd)"/$GIT_DIR/config
test_rev_parse 'inside'       false false true ''
cd sub/dir || exit 1
GIT_DIR=../../../repo.git
GIT_CONFIG="$(pwd)"/$GIT_DIR/config
test_rev_parse 'subdirectory' false false true sub/dir/
cd ../../.. || exit 1

say "core.worktree = absolute path"
GIT_DIR=$(pwd)/repo.git
GIT_CONFIG=$GIT_DIR/config
git config core.worktree "$(pwd)/work"
test_rev_parse 'outside'      false false false
cd work2
test_rev_parse 'outside2'     false false false
cd ../work || exit 1
test_rev_parse 'inside'       false false true ''
cd sub/dir || exit 1
test_rev_parse 'subdirectory' false false true sub/dir/
cd ../../.. || exit 1

say "GIT_WORK_TREE=relative path (override core.worktree)"
GIT_DIR=$(pwd)/repo.git
GIT_CONFIG=$GIT_DIR/config
git config core.worktree non-existent
GIT_WORK_TREE=work
export GIT_WORK_TREE
test_rev_parse 'outside'      false false false
cd work2
test_rev_parse 'outside'      false false false
cd ../work || exit 1
GIT_WORK_TREE=.
test_rev_parse 'inside'       false false true ''
cd sub/dir || exit 1
GIT_WORK_TREE=../..
test_rev_parse 'subdirectory' false false true sub/dir/
cd ../../.. || exit 1

mv work repo.git/work
mv work2 repo.git/work2

say "GIT_WORK_TREE=absolute path, work tree below git dir"
GIT_DIR=$(pwd)/repo.git
GIT_CONFIG=$GIT_DIR/config
GIT_WORK_TREE=$(pwd)/repo.git/work
test_rev_parse 'outside'              false false false
cd repo.git || exit 1
test_rev_parse 'in repo.git'              false true  false
cd objects || exit 1
test_rev_parse 'in repo.git/objects'      false true  false
cd ../work2 || exit 1
test_rev_parse 'in repo.git/work2'      false true  false
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

test_expect_success '_gently() groks relative GIT_DIR & GIT_WORK_TREE' '
	(cd repo.git/work/sub/dir &&
	GIT_DIR=../../.. GIT_WORK_TREE=../.. GIT_PAGER= \
		git diff --exit-code tracked &&
	echo changed > tracked &&
	! GIT_DIR=../../.. GIT_WORK_TREE=../.. GIT_PAGER= \
		git diff --exit-code tracked)
'
cat > diff-index-cached.expected <<\EOF
:000000 100644 0000000000000000000000000000000000000000 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 A	sub/dir/tracked
EOF
cat > diff-index.expected <<\EOF
:000000 100644 0000000000000000000000000000000000000000 0000000000000000000000000000000000000000 A	sub/dir/tracked
EOF


test_expect_success 'git diff-index' '
	GIT_DIR=repo.git GIT_WORK_TREE=repo.git/work git diff-index $EMPTY_TREE > result &&
	test_cmp diff-index.expected result &&
	GIT_DIR=repo.git git diff-index --cached $EMPTY_TREE > result &&
	test_cmp diff-index-cached.expected result
'
cat >diff-files.expected <<\EOF
:100644 100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0000000000000000000000000000000000000000 M	sub/dir/tracked
EOF

test_expect_success 'git diff-files' '
	GIT_DIR=repo.git GIT_WORK_TREE=repo.git/work git diff-files > result &&
	test_cmp diff-files.expected result
'

cat >diff-TREE.expected <<\EOF
diff --git a/sub/dir/tracked b/sub/dir/tracked
new file mode 100644
index 0000000..5ea2ed4
--- /dev/null
+++ b/sub/dir/tracked
@@ -0,0 +1 @@
+changed
EOF
cat >diff-TREE-cached.expected <<\EOF
diff --git a/sub/dir/tracked b/sub/dir/tracked
new file mode 100644
index 0000000..e69de29
EOF
cat >diff-FILES.expected <<\EOF
diff --git a/sub/dir/tracked b/sub/dir/tracked
index e69de29..5ea2ed4 100644
--- a/sub/dir/tracked
+++ b/sub/dir/tracked
@@ -0,0 +1 @@
+changed
EOF

test_expect_success 'git diff' '
	GIT_DIR=repo.git GIT_WORK_TREE=repo.git/work git diff $EMPTY_TREE > result &&
	test_cmp diff-TREE.expected result &&
	GIT_DIR=repo.git git diff --cached $EMPTY_TREE > result &&
	test_cmp diff-TREE-cached.expected result &&
	GIT_DIR=repo.git GIT_WORK_TREE=repo.git/work git diff > result &&
	test_cmp diff-FILES.expected result
'

test_expect_success 'git grep' '
	(cd repo.git/work/sub &&
	GIT_DIR=../.. GIT_WORK_TREE=.. git grep -l changed | grep dir/tracked)
'

test_expect_success 'git commit' '
	(
		cd repo.git &&
		GIT_DIR=. GIT_WORK_TREE=work git commit -a -m done
	)
'

test_expect_success 'absolute pathspec should fail gracefully' '
	(
		cd repo.git || exit 1
		git config --unset core.worktree
		test_must_fail git log HEAD -- /home
	)
'

test_expect_success 'make_relative_path handles double slashes in GIT_DIR' '
	: > dummy_file
	echo git --git-dir="$(pwd)//repo.git" --work-tree="$(pwd)" add dummy_file &&
	git --git-dir="$(pwd)//repo.git" --work-tree="$(pwd)" add dummy_file
'

test_done
