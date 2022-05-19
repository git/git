#!/bin/sh

test_description='test local clone'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

repo_is_hardlinked() {
	find "$1/objects" -type f -links 1 >output &&
	test_line_count = 0 output
}

test_expect_success 'preparing origin repository' '
	: >file && but add . && but cummit -m1 &&
	but clone --bare . a.but &&
	but clone --bare . x &&
	test "$(cd a.but && but config --bool core.bare)" = true &&
	test "$(cd x && but config --bool core.bare)" = true &&
	but bundle create b1.bundle --all &&
	but bundle create b2.bundle main &&
	mkdir dir &&
	cp b1.bundle dir/b3 &&
	cp b1.bundle b4
'

test_expect_success 'local clone without .but suffix' '
	but clone -l -s a b &&
	(cd b &&
	test "$(but config --bool core.bare)" = false &&
	but fetch)
'

test_expect_success 'local clone with .but suffix' '
	but clone -l -s a.but c &&
	(cd c && but fetch)
'

test_expect_success 'local clone from x' '
	but clone -l -s x y &&
	(cd y && but fetch)
'

test_expect_success 'local clone from x.but that does not exist' '
	test_must_fail but clone -l -s x.but z
'

test_expect_success 'With -no-hardlinks, local will make a copy' '
	but clone --bare --no-hardlinks x w &&
	! repo_is_hardlinked w
'

test_expect_success 'Even without -l, local will make a hardlink' '
	rm -fr w &&
	but clone -l --bare x w &&
	repo_is_hardlinked w
'

test_expect_success 'local clone of repo with nonexistent ref in HEAD' '
	echo "ref: refs/heads/nonexistent" > a.but/HEAD &&
	but clone a d &&
	(cd d &&
	but fetch &&
	test ! -e .but/refs/remotes/origin/HEAD)
'

test_expect_success 'bundle clone without .bundle suffix' '
	but clone dir/b3 &&
	(cd b3 && but fetch)
'

test_expect_success 'bundle clone with .bundle suffix' '
	but clone b1.bundle &&
	(cd b1 && but fetch)
'

test_expect_success 'bundle clone from b4' '
	but clone b4 bdl &&
	(cd bdl && but fetch)
'

test_expect_success 'bundle clone from b4.bundle that does not exist' '
	test_must_fail but clone b4.bundle bb
'

test_expect_success 'bundle clone with nonexistent HEAD' '
	but clone b2.bundle b2 &&
	(cd b2 &&
	but fetch &&
	test_must_fail but rev-parse --verify refs/heads/main)
'

test_expect_success 'clone empty repository' '
	mkdir empty &&
	(cd empty &&
	 but init &&
	 but config receive.denyCurrentBranch warn) &&
	but clone empty empty-clone &&
	test_tick &&
	(cd empty-clone &&
	 echo "content" >> foo &&
	 but add foo &&
	 but cummit -m "Initial cummit" &&
	 but push origin main &&
	 expected=$(but rev-parse main) &&
	 actual=$(but --but-dir=../empty/.but rev-parse main) &&
	 test $actual = $expected)
'

test_expect_success 'clone empty repository, and then push should not segfault.' '
	rm -fr empty/ empty-clone/ &&
	mkdir empty &&
	(cd empty && but init) &&
	but clone empty empty-clone &&
	(cd empty-clone &&
	test_must_fail but push)
'

test_expect_success 'cloning non-existent directory fails' '
	rm -rf does-not-exist &&
	test_must_fail but clone does-not-exist
'

test_expect_success 'cloning non-but directory fails' '
	rm -rf not-a-but-repo not-a-but-repo-clone &&
	mkdir not-a-but-repo &&
	test_must_fail but clone not-a-but-repo not-a-but-repo-clone
'

test_expect_success 'cloning file:// does not hardlink' '
	but clone --bare file://"$(pwd)"/a non-local &&
	! repo_is_hardlinked non-local
'

test_expect_success 'cloning a local path with --no-local does not hardlink' '
	but clone --bare --no-local a force-nonlocal &&
	! repo_is_hardlinked force-nonlocal
'

test_expect_success 'cloning locally respects "-u" for fetching refs' '
	test_must_fail but clone --bare -u false a should_not_work.but
'

test_done
