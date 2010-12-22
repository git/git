#!/bin/sh

test_description='sparse checkout tests

* (tag: removed, master) removed
| D	sub/added
* (HEAD, tag: top) modified and added
| M	init.t
| A	sub/added
* (tag: init) init
  A	init.t
'

. ./test-lib.sh

test_expect_success 'setup' '
	cat >expected <<-\EOF &&
	100644 77f0ba1734ed79d12881f81b36ee134de6a3327b 0	init.t
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	sub/added
	100644 e69de29bb2d1d6434b8b29ae775ad8c2e48c5391 0	subsub/added
	EOF
	cat >expected.swt <<-\EOF &&
	H init.t
	H sub/added
	H subsub/added
	EOF

	test_commit init &&
	echo modified >>init.t &&
	mkdir sub subsub &&
	touch sub/added subsub/added &&
	git add init.t sub/added subsub/added &&
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
	git read-tree -m -u HEAD &&
	git ls-files --stage >result &&
	test_cmp expected result &&
	git ls-files -t >result &&
	test_cmp expected.swt result
'

test_expect_success 'read-tree with .git/info/sparse-checkout but disabled' '
	echo >.git/info/sparse-checkout &&
	git read-tree -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt result &&
	test -f init.t &&
	test -f sub/added
'

test_expect_success 'read-tree --no-sparse-checkout with empty .git/info/sparse-checkout and enabled' '
	git config core.sparsecheckout true &&
	echo >.git/info/sparse-checkout &&
	git read-tree --no-sparse-checkout -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt result &&
	test -f init.t &&
	test -f sub/added
'

test_expect_success 'read-tree with empty .git/info/sparse-checkout' '
	git config core.sparsecheckout true &&
	echo >.git/info/sparse-checkout &&
	test_must_fail git read-tree -m -u HEAD &&
	git ls-files --stage >result &&
	test_cmp expected result &&
	git ls-files -t >result &&
	test_cmp expected.swt result &&
	test -f init.t &&
	test -f sub/added
'

test_expect_success 'match directories with trailing slash' '
	cat >expected.swt-noinit <<-\EOF &&
	S init.t
	H sub/added
	S subsub/added
	EOF

	echo sub/ > .git/info/sparse-checkout &&
	git read-tree -m -u HEAD &&
	git ls-files -t > result &&
	test_cmp expected.swt-noinit result &&
	test ! -f init.t &&
	test -f sub/added
'

test_expect_success 'match directories without trailing slash' '
	echo sub >>.git/info/sparse-checkout &&
	git read-tree -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt-noinit result &&
	test ! -f init.t &&
	test -f sub/added
'

test_expect_success 'match directory pattern' '
	echo "s?b" >>.git/info/sparse-checkout &&
	git read-tree -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt-noinit result &&
	test ! -f init.t &&
	test -f sub/added
'

test_expect_success 'checkout area changes' '
	cat >expected.swt-nosub <<-\EOF &&
	H init.t
	S sub/added
	S subsub/added
	EOF

	echo init.t >.git/info/sparse-checkout &&
	git read-tree -m -u HEAD &&
	git ls-files -t >result &&
	test_cmp expected.swt-nosub result &&
	test -f init.t &&
	test ! -f sub/added
'

test_expect_success 'read-tree updates worktree, absent case' '
	echo sub/added >.git/info/sparse-checkout &&
	git checkout -f top &&
	git read-tree -m -u HEAD^ &&
	test ! -f init.t
'

test_expect_success 'read-tree updates worktree, dirty case' '
	echo sub/added >.git/info/sparse-checkout &&
	git checkout -f top &&
	echo dirty >init.t &&
	git read-tree -m -u HEAD^ &&
	grep -q dirty init.t &&
	rm init.t
'

test_expect_success 'read-tree removes worktree, dirty case' '
	echo init.t >.git/info/sparse-checkout &&
	git checkout -f top &&
	echo dirty >added &&
	git read-tree -m -u HEAD^ &&
	grep -q dirty added
'

test_expect_success 'read-tree adds to worktree, absent case' '
	echo init.t >.git/info/sparse-checkout &&
	git checkout -f removed &&
	git read-tree -u -m HEAD^ &&
	test ! -f sub/added
'

test_expect_success 'read-tree adds to worktree, dirty case' '
	echo init.t >.git/info/sparse-checkout &&
	git checkout -f removed &&
	mkdir sub &&
	echo dirty >sub/added &&
	git read-tree -u -m HEAD^ &&
	grep -q dirty sub/added
'

test_expect_success 'index removal and worktree narrowing at the same time' '
	>empty &&
	echo init.t >.git/info/sparse-checkout &&
	echo sub/added >>.git/info/sparse-checkout &&
	git checkout -f top &&
	echo init.t >.git/info/sparse-checkout &&
	git checkout removed &&
	git ls-files sub/added >result &&
	test ! -f sub/added &&
	test_cmp empty result
'

test_expect_success 'read-tree --reset removes outside worktree' '
	>empty &&
	echo init.t >.git/info/sparse-checkout &&
	git checkout -f top &&
	git reset --hard removed &&
	git ls-files sub/added >result &&
	test_cmp empty result
'

test_done
