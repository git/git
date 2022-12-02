#!/bin/sh

test_description='sparse checkout tests

* (tag: removed, main) removed
| D	sub/added
* (HEAD, tag: top) modified and added
| M	init.t
| A	sub/added
* (tag: init) init
  A	init.t
'

TEST_CREATE_REPO_NO_TEMPLATE=1
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

test_expect_success 'setup' '
	test_commit init &&
	echo modified >>init.t &&

	cat >expected <<-EOF &&
	100644 $(git hash-object init.t) 0	init.t
	100644 $EMPTY_BLOB 0	sub/added
	100644 $EMPTY_BLOB 0	sub/addedtoo
	100644 $EMPTY_BLOB 0	subsub/added
	EOF
	cat >expected.swt <<-\EOF &&
	H init.t
	H sub/added
	H sub/addedtoo
	H subsub/added
	EOF

	mkdir sub subsub &&
	touch sub/added sub/addedtoo subsub/added &&
	git add init.t sub/added sub/addedtoo subsub/added &&
	git commit -m "modified and added" &&
	git tag top &&
	git rm sub/added &&
	git commit -m removed &&
	git tag removed &&
	git checkout top &&
	git ls-files --stage >result &&
	test_cmp expected result
'

test_expect_success 'read-tree without .git/info/sparse-checkout' '
	read_tree_u_must_succeed -m -u HEAD &&
	git ls-files --stage >result &&
	test_cmp expected result &&
	git ls-files -t >result &&
	test_cmp expected.swt result
'

test_expect_success 'read-tree with .git/info/sparse-checkout but disabled' '
	mkdir .git/info &&
	echo >.git/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt result &&
	test_path_is_file init.t &&
	test_path_is_file sub/added
'

test_expect_success 'read-tree --no-sparse-checkout with empty .git/info/sparse-checkout and enabled' '
	git config core.sparsecheckout true &&
	echo >.git/info/sparse-checkout &&
	read_tree_u_must_succeed --no-sparse-checkout -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt result &&
	test_path_is_file init.t &&
	test_path_is_file sub/added
'

test_expect_success 'read-tree with empty .git/info/sparse-checkout' '
	git config core.sparsecheckout true &&
	echo >.git/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	git ls-files --stage >result &&
	test_cmp expected result &&
	git ls-files -t >result &&
	cat >expected.swt <<-\EOF &&
	S init.t
	S sub/added
	S sub/addedtoo
	S subsub/added
	EOF
	test_cmp expected.swt result &&
	test_path_is_missing init.t &&
	test_path_is_missing sub/added
'

test_expect_success 'match directories with trailing slash' '
	cat >expected.swt-noinit <<-\EOF &&
	S init.t
	H sub/added
	H sub/addedtoo
	S subsub/added
	EOF

	echo sub/ > .git/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	git ls-files -t > result &&
	test_cmp expected.swt-noinit result &&
	test_path_is_missing init.t &&
	test_path_is_file sub/added
'

test_expect_success 'match directories without trailing slash' '
	echo sub >.git/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt-noinit result &&
	test_path_is_missing init.t &&
	test_path_is_file sub/added
'

test_expect_success 'match directories with negated patterns' '
	cat >expected.swt-negation <<\EOF &&
S init.t
S sub/added
H sub/addedtoo
S subsub/added
EOF

	cat >.git/info/sparse-checkout <<\EOF &&
sub
!sub/added
EOF
	git read-tree -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt-negation result &&
	test_path_is_missing init.t &&
	test_path_is_missing sub/added &&
	test_path_is_file sub/addedtoo
'

test_expect_success 'match directories with negated patterns (2)' '
	cat >expected.swt-negation2 <<\EOF &&
H init.t
H sub/added
S sub/addedtoo
H subsub/added
EOF

	cat >.git/info/sparse-checkout <<\EOF &&
/*
!sub
sub/added
EOF
	git read-tree -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt-negation2 result &&
	test_path_is_file init.t &&
	test_path_is_file sub/added &&
	test_path_is_missing sub/addedtoo
'

test_expect_success 'match directory pattern' '
	echo "s?b" >.git/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt-noinit result &&
	test_path_is_missing init.t &&
	test_path_is_file sub/added
'

test_expect_success 'checkout area changes' '
	cat >expected.swt-nosub <<-\EOF &&
	H init.t
	S sub/added
	S sub/addedtoo
	S subsub/added
	EOF

	echo init.t >.git/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt-nosub result &&
	test_path_is_file init.t &&
	test_path_is_missing sub/added
'

test_expect_success 'read-tree updates worktree, absent case' '
	echo sub/added >.git/info/sparse-checkout &&
	git checkout -f top &&
	read_tree_u_must_succeed -m -u HEAD^ &&
	test_path_is_missing init.t
'

test_expect_success 'read-tree will not throw away dirty changes, non-sparse' '
	echo "/*" >.git/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&

	echo dirty >init.t &&
	read_tree_u_must_fail -m -u HEAD^ &&
	test_path_is_file init.t &&
	grep -q dirty init.t
'

test_expect_success 'read-tree will not throw away dirty changes, sparse' '
	echo "/*" >.git/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&

	echo dirty >init.t &&
	echo sub/added >.git/info/sparse-checkout &&
	read_tree_u_must_fail -m -u HEAD^ &&
	test_path_is_file init.t &&
	grep -q dirty init.t
'

test_expect_success 'read-tree updates worktree, dirty case' '
	echo sub/added >.git/info/sparse-checkout &&
	git checkout -f top &&
	echo dirty >init.t &&
	read_tree_u_must_fail -m -u HEAD^ &&
	grep -q dirty init.t &&
	rm init.t
'

test_expect_success 'read-tree removes worktree, dirty case' '
	echo init.t >.git/info/sparse-checkout &&
	git checkout -f top &&
	echo dirty >added &&
	read_tree_u_must_succeed -m -u HEAD^ &&
	grep -q dirty added
'

test_expect_success 'read-tree adds to worktree, absent case' '
	echo init.t >.git/info/sparse-checkout &&
	git checkout -f removed &&
	read_tree_u_must_succeed -u -m HEAD^ &&
	test_path_is_missing sub/added
'

test_expect_success 'read-tree adds to worktree, dirty case' '
	echo init.t >.git/info/sparse-checkout &&
	git checkout -f removed &&
	mkdir sub &&
	echo dirty >sub/added &&
	read_tree_u_must_succeed -u -m HEAD^ &&
	grep -q dirty sub/added
'

test_expect_success 'index removal and worktree narrowing at the same time' '
	echo init.t >.git/info/sparse-checkout &&
	echo sub/added >>.git/info/sparse-checkout &&
	git checkout -f top &&
	echo init.t >.git/info/sparse-checkout &&
	git checkout removed &&
	git ls-files sub/added >result &&
	test_path_is_missing sub/added &&
	test_must_be_empty result
'

test_expect_success 'read-tree --reset removes outside worktree' '
	echo init.t >.git/info/sparse-checkout &&
	git checkout -f top &&
	git reset --hard removed &&
	git ls-files sub/added >result &&
	test_must_be_empty result
'

test_expect_success 'print warnings when some worktree updates disabled' '
	echo sub >.git/info/sparse-checkout &&
	git checkout -f init &&
	mkdir sub &&
	touch sub/added sub/addedtoo &&
	# Use -q to suppress "Previous HEAD position" and "Head is now at" msgs
	git checkout -q top 2>actual &&
	cat >expected <<\EOF &&
warning: The following paths were already present and thus not updated despite sparse patterns:
	sub/added
	sub/addedtoo

After fixing the above paths, you may want to run `git sparse-checkout reapply`.
EOF
	test_cmp expected actual
'

test_expect_success 'checkout without --ignore-skip-worktree-bits' '
	echo "*" >.git/info/sparse-checkout &&
	git checkout -f top &&
	test_path_is_file init.t &&
	echo sub >.git/info/sparse-checkout &&
	git checkout &&
	echo modified >> sub/added &&
	git checkout . &&
	test_path_is_missing init.t &&
	git diff --exit-code HEAD
'

test_expect_success 'checkout with --ignore-skip-worktree-bits' '
	echo "*" >.git/info/sparse-checkout &&
	git checkout -f top &&
	test_path_is_file init.t &&
	echo sub >.git/info/sparse-checkout &&
	git checkout &&
	echo modified >> sub/added &&
	git checkout --ignore-skip-worktree-bits . &&
	test_path_is_file init.t &&
	git diff --exit-code HEAD
'

test_done
