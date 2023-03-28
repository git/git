#!/bin/sh

test_description='Test the dir-iterator functionality'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	mkdir -p dir &&
	mkdir -p dir/a/b/c/ &&
	>dir/b &&
	>dir/c &&
	mkdir -p dir/d/e/d/ &&
	>dir/a/b/c/d &&
	>dir/a/e &&
	>dir/d/e/d/a &&

	mkdir -p dir2/a/b/c/ &&
	>dir2/a/b/c/d
'

test_expect_success 'dir-iterator should iterate through all files' '
	cat >expected-iteration-sorted-output <<-EOF &&
	[d] (a) [a] ./dir/a
	[d] (a/b) [b] ./dir/a/b
	[d] (a/b/c) [c] ./dir/a/b/c
	[d] (d) [d] ./dir/d
	[d] (d/e) [e] ./dir/d/e
	[d] (d/e/d) [d] ./dir/d/e/d
	[f] (a/b/c/d) [d] ./dir/a/b/c/d
	[f] (a/e) [e] ./dir/a/e
	[f] (b) [b] ./dir/b
	[f] (c) [c] ./dir/c
	[f] (d/e/d/a) [a] ./dir/d/e/d/a
	EOF

	test-tool dir-iterator ./dir >out &&
	sort out >./actual-iteration-sorted-output &&

	test_cmp expected-iteration-sorted-output actual-iteration-sorted-output
'

test_expect_success 'dir-iterator should list files in the correct order' '
	cat >expected-pre-order-output <<-EOF &&
	[d] (a) [a] ./dir2/a
	[d] (a/b) [b] ./dir2/a/b
	[d] (a/b/c) [c] ./dir2/a/b/c
	[f] (a/b/c/d) [d] ./dir2/a/b/c/d
	EOF

	test-tool dir-iterator ./dir2 >actual-pre-order-output &&

	test_cmp expected-pre-order-output actual-pre-order-output
'

test_expect_success 'begin should fail upon inexistent paths' '
	test_must_fail test-tool dir-iterator ./inexistent-path \
		>actual-inexistent-path-output &&
	echo "dir_iterator_begin failure: ENOENT" >expected-inexistent-path-output &&
	test_cmp expected-inexistent-path-output actual-inexistent-path-output
'

test_expect_success 'begin should fail upon non directory paths' '
	test_must_fail test-tool dir-iterator ./dir/b >actual-non-dir-output &&
	echo "dir_iterator_begin failure: ENOTDIR" >expected-non-dir-output &&
	test_cmp expected-non-dir-output actual-non-dir-output
'

test_expect_success POSIXPERM,SANITY 'advance should not fail on errors by default' '
	cat >expected-no-permissions-output <<-EOF &&
	[d] (a) [a] ./dir3/a
	EOF

	mkdir -p dir3/a &&
	>dir3/a/b &&
	chmod 0 dir3/a &&

	test-tool dir-iterator ./dir3 >actual-no-permissions-output &&
	test_cmp expected-no-permissions-output actual-no-permissions-output &&
	chmod 755 dir3/a &&
	rm -rf dir3
'

test_expect_success POSIXPERM,SANITY 'advance should fail on errors, w/ pedantic flag' '
	cat >expected-no-permissions-pedantic-output <<-EOF &&
	[d] (a) [a] ./dir3/a
	dir_iterator_advance failure
	EOF

	mkdir -p dir3/a &&
	>dir3/a/b &&
	chmod 0 dir3/a &&

	test_must_fail test-tool dir-iterator --pedantic ./dir3 \
		>actual-no-permissions-pedantic-output &&
	test_cmp expected-no-permissions-pedantic-output \
		actual-no-permissions-pedantic-output &&
	chmod 755 dir3/a &&
	rm -rf dir3
'

test_expect_success SYMLINKS 'setup dirs with symlinks' '
	mkdir -p dir4/a &&
	mkdir -p dir4/b/c &&
	>dir4/a/d &&
	ln -s d dir4/a/e &&
	ln -s ../b dir4/a/f &&

	ln -s dir4 dir5
'

test_expect_success SYMLINKS 'dir-iterator should not follow symlinks by default' '
	cat >expected-no-follow-sorted-output <<-EOF &&
	[d] (a) [a] ./dir4/a
	[d] (b) [b] ./dir4/b
	[d] (b/c) [c] ./dir4/b/c
	[f] (a/d) [d] ./dir4/a/d
	[s] (a/e) [e] ./dir4/a/e
	[s] (a/f) [f] ./dir4/a/f
	EOF

	test-tool dir-iterator ./dir4 >out &&
	sort out >actual-no-follow-sorted-output &&

	test_cmp expected-no-follow-sorted-output actual-no-follow-sorted-output
'

test_expect_success SYMLINKS 'dir-iterator does not resolve top-level symlinks' '
	test_must_fail test-tool dir-iterator ./dir5 >out &&

	grep "ENOTDIR" out
'

test_done
