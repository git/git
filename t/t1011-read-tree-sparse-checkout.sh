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

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-read-tree.sh

test_expect_success 'setup' '
	test_cummit init &&
	echo modified >>init.t &&

	cat >expected <<-EOF &&
	100644 $(but hash-object init.t) 0	init.t
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
	but add init.t sub/added sub/addedtoo subsub/added &&
	but cummit -m "modified and added" &&
	but tag top &&
	but rm sub/added &&
	but cummit -m removed &&
	but tag removed &&
	but checkout top &&
	but ls-files --stage >result &&
	test_cmp expected result
'

test_expect_success 'read-tree without .but/info/sparse-checkout' '
	read_tree_u_must_succeed -m -u HEAD &&
	but ls-files --stage >result &&
	test_cmp expected result &&
	but ls-files -t >result &&
	test_cmp expected.swt result
'

test_expect_success 'read-tree with .but/info/sparse-checkout but disabled' '
	echo >.but/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	but ls-files -t >result &&
	test_cmp expected.swt result &&
	test -f init.t &&
	test -f sub/added
'

test_expect_success 'read-tree --no-sparse-checkout with empty .but/info/sparse-checkout and enabled' '
	but config core.sparsecheckout true &&
	echo >.but/info/sparse-checkout &&
	read_tree_u_must_succeed --no-sparse-checkout -m -u HEAD &&
	but ls-files -t >result &&
	test_cmp expected.swt result &&
	test -f init.t &&
	test -f sub/added
'

test_expect_success 'read-tree with empty .but/info/sparse-checkout' '
	but config core.sparsecheckout true &&
	echo >.but/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	but ls-files --stage >result &&
	test_cmp expected result &&
	but ls-files -t >result &&
	cat >expected.swt <<-\EOF &&
	S init.t
	S sub/added
	S sub/addedtoo
	S subsub/added
	EOF
	test_cmp expected.swt result &&
	! test -f init.t &&
	! test -f sub/added
'

test_expect_success 'match directories with trailing slash' '
	cat >expected.swt-noinit <<-\EOF &&
	S init.t
	H sub/added
	H sub/addedtoo
	S subsub/added
	EOF

	echo sub/ > .but/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	but ls-files -t > result &&
	test_cmp expected.swt-noinit result &&
	test ! -f init.t &&
	test -f sub/added
'

test_expect_success 'match directories without trailing slash' '
	echo sub >.but/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	but ls-files -t >result &&
	test_cmp expected.swt-noinit result &&
	test ! -f init.t &&
	test -f sub/added
'

test_expect_success 'match directories with negated patterns' '
	cat >expected.swt-negation <<\EOF &&
S init.t
S sub/added
H sub/addedtoo
S subsub/added
EOF

	cat >.but/info/sparse-checkout <<\EOF &&
sub
!sub/added
EOF
	but read-tree -m -u HEAD &&
	but ls-files -t >result &&
	test_cmp expected.swt-negation result &&
	test ! -f init.t &&
	test ! -f sub/added &&
	test -f sub/addedtoo
'

test_expect_success 'match directories with negated patterns (2)' '
	cat >expected.swt-negation2 <<\EOF &&
H init.t
H sub/added
S sub/addedtoo
H subsub/added
EOF

	cat >.but/info/sparse-checkout <<\EOF &&
/*
!sub
sub/added
EOF
	but read-tree -m -u HEAD &&
	but ls-files -t >result &&
	test_cmp expected.swt-negation2 result &&
	test -f init.t &&
	test -f sub/added &&
	test ! -f sub/addedtoo
'

test_expect_success 'match directory pattern' '
	echo "s?b" >.but/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	but ls-files -t >result &&
	test_cmp expected.swt-noinit result &&
	test ! -f init.t &&
	test -f sub/added
'

test_expect_success 'checkout area changes' '
	cat >expected.swt-nosub <<-\EOF &&
	H init.t
	S sub/added
	S sub/addedtoo
	S subsub/added
	EOF

	echo init.t >.but/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&
	but ls-files -t >result &&
	test_cmp expected.swt-nosub result &&
	test -f init.t &&
	test ! -f sub/added
'

test_expect_success 'read-tree updates worktree, absent case' '
	echo sub/added >.but/info/sparse-checkout &&
	but checkout -f top &&
	read_tree_u_must_succeed -m -u HEAD^ &&
	test ! -f init.t
'

test_expect_success 'read-tree will not throw away dirty changes, non-sparse' '
	echo "/*" >.but/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&

	echo dirty >init.t &&
	read_tree_u_must_fail -m -u HEAD^ &&
	test_path_is_file init.t &&
	grep -q dirty init.t
'

test_expect_success 'read-tree will not throw away dirty changes, sparse' '
	echo "/*" >.but/info/sparse-checkout &&
	read_tree_u_must_succeed -m -u HEAD &&

	echo dirty >init.t &&
	echo sub/added >.but/info/sparse-checkout &&
	read_tree_u_must_fail -m -u HEAD^ &&
	test_path_is_file init.t &&
	grep -q dirty init.t
'

test_expect_success 'read-tree updates worktree, dirty case' '
	echo sub/added >.but/info/sparse-checkout &&
	but checkout -f top &&
	echo dirty >init.t &&
	read_tree_u_must_fail -m -u HEAD^ &&
	grep -q dirty init.t &&
	rm init.t
'

test_expect_success 'read-tree removes worktree, dirty case' '
	echo init.t >.but/info/sparse-checkout &&
	but checkout -f top &&
	echo dirty >added &&
	read_tree_u_must_succeed -m -u HEAD^ &&
	grep -q dirty added
'

test_expect_success 'read-tree adds to worktree, absent case' '
	echo init.t >.but/info/sparse-checkout &&
	but checkout -f removed &&
	read_tree_u_must_succeed -u -m HEAD^ &&
	test ! -f sub/added
'

test_expect_success 'read-tree adds to worktree, dirty case' '
	echo init.t >.but/info/sparse-checkout &&
	but checkout -f removed &&
	mkdir sub &&
	echo dirty >sub/added &&
	read_tree_u_must_succeed -u -m HEAD^ &&
	grep -q dirty sub/added
'

test_expect_success 'index removal and worktree narrowing at the same time' '
	echo init.t >.but/info/sparse-checkout &&
	echo sub/added >>.but/info/sparse-checkout &&
	but checkout -f top &&
	echo init.t >.but/info/sparse-checkout &&
	but checkout removed &&
	but ls-files sub/added >result &&
	test ! -f sub/added &&
	test_must_be_empty result
'

test_expect_success 'read-tree --reset removes outside worktree' '
	echo init.t >.but/info/sparse-checkout &&
	but checkout -f top &&
	but reset --hard removed &&
	but ls-files sub/added >result &&
	test_must_be_empty result
'

test_expect_success 'print warnings when some worktree updates disabled' '
	echo sub >.but/info/sparse-checkout &&
	but checkout -f init &&
	mkdir sub &&
	touch sub/added sub/addedtoo &&
	# Use -q to suppress "Previous HEAD position" and "Head is now at" msgs
	but checkout -q top 2>actual &&
	cat >expected <<\EOF &&
warning: The following paths were already present and thus not updated despite sparse patterns:
	sub/added
	sub/addedtoo

After fixing the above paths, you may want to run `but sparse-checkout reapply`.
EOF
	test_cmp expected actual
'

test_expect_success 'checkout without --ignore-skip-worktree-bits' '
	echo "*" >.but/info/sparse-checkout &&
	but checkout -f top &&
	test_path_is_file init.t &&
	echo sub >.but/info/sparse-checkout &&
	but checkout &&
	echo modified >> sub/added &&
	but checkout . &&
	test_path_is_missing init.t &&
	but diff --exit-code HEAD
'

test_expect_success 'checkout with --ignore-skip-worktree-bits' '
	echo "*" >.but/info/sparse-checkout &&
	but checkout -f top &&
	test_path_is_file init.t &&
	echo sub >.but/info/sparse-checkout &&
	but checkout &&
	echo modified >> sub/added &&
	but checkout --ignore-skip-worktree-bits . &&
	test_path_is_file init.t &&
	but diff --exit-code HEAD
'

test_done
