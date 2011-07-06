#!/bin/sh

test_description=clone

. ./test-lib.sh

test_expect_success setup '

	rm -fr .git &&
	test_create_repo src &&
	(
		cd src
		>file
		git add file
		git commit -m initial
	)

'

test_expect_success 'clone with excess parameters (1)' '

	rm -fr dst &&
	test_must_fail git clone -n src dst junk

'

test_expect_success 'clone with excess parameters (2)' '

	rm -fr dst &&
	test_must_fail git clone -n "file://$(pwd)/src" dst junk

'

test_expect_success 'output from clone' '
	rm -fr dst &&
	git clone -n "file://$(pwd)/src" dst >output &&
	test $(grep Initialized output | wc -l) = 1
'

test_expect_success 'clone does not keep pack' '

	rm -fr dst &&
	git clone -n "file://$(pwd)/src" dst &&
	! test -f dst/file &&
	! (echo dst/.git/objects/pack/pack-* | grep "\.keep")

'

test_expect_success 'clone checks out files' '

	rm -fr dst &&
	git clone src dst &&
	test -f dst/file

'

test_expect_success 'clone respects GIT_WORK_TREE' '

	GIT_WORK_TREE=worktree git clone src bare &&
	test -f bare/config &&
	test -f worktree/file

'

test_expect_success 'clone creates intermediate directories' '

	git clone src long/path/to/dst &&
	test -f long/path/to/dst/file

'

test_expect_success 'clone creates intermediate directories for bare repo' '

	git clone --bare src long/path/to/bare/dst &&
	test -f long/path/to/bare/dst/config

'

test_expect_success 'clone --mirror' '

	git clone --mirror src mirror &&
	test -f mirror/HEAD &&
	test ! -f mirror/file &&
	FETCH="$(cd mirror && git config remote.origin.fetch)" &&
	test "+refs/*:refs/*" = "$FETCH" &&
	MIRROR="$(cd mirror && git config --bool remote.origin.mirror)" &&
	test "$MIRROR" = true

'

test_expect_success 'clone --bare names the local repository <name>.git' '

	git clone --bare src &&
	test -d src.git

'

test_expect_success 'clone --mirror does not repeat tags' '

	(cd src &&
	 git tag some-tag HEAD) &&
	git clone --mirror src mirror2 &&
	(cd mirror2 &&
	 git show-ref 2> clone.err > clone.out) &&
	test_must_fail grep Duplicate mirror2/clone.err &&
	grep some-tag mirror2/clone.out

'

test_done
