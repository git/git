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

test_expect_success C_LOCALE_OUTPUT 'output from clone' '
	rm -fr dst &&
	git clone -n "file://$(pwd)/src" dst >output &&
	test $(grep Clon output | wc -l) = 1
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

test_expect_success 'clone to destination with trailing /' '

	git clone src target-1/ &&
	T=$( cd target-1 && git rev-parse HEAD ) &&
	S=$( cd src && git rev-parse HEAD ) &&
	test "$T" = "$S"

'

test_expect_success 'clone to destination with extra trailing /' '

	git clone src target-2/// &&
	T=$( cd target-2 && git rev-parse HEAD ) &&
	S=$( cd src && git rev-parse HEAD ) &&
	test "$T" = "$S"

'

test_expect_success 'clone to an existing empty directory' '
	mkdir target-3 &&
	git clone src target-3 &&
	T=$( cd target-3 && git rev-parse HEAD ) &&
	S=$( cd src && git rev-parse HEAD ) &&
	test "$T" = "$S"
'

test_expect_success 'clone to an existing non-empty directory' '
	mkdir target-4 &&
	>target-4/Fakefile &&
	test_must_fail git clone src target-4
'

test_expect_success 'clone to an existing path' '
	>target-5 &&
	test_must_fail git clone src target-5
'

test_expect_success 'clone a void' '
	mkdir src-0 &&
	(
		cd src-0 && git init
	) &&
	git clone "file://$(pwd)/src-0" target-6 2>err-6 &&
	! grep "fatal:" err-6 &&
	(
		cd src-0 && test_commit A
	) &&
	git clone "file://$(pwd)/src-0" target-7 2>err-7 &&
	! grep "fatal:" err-7 &&
	# There is no reason to insist they are bit-for-bit
	# identical, but this test should suffice for now.
	test_cmp target-6/.git/config target-7/.git/config
'

test_expect_success 'clone respects global branch.autosetuprebase' '
	(
		test_config="$HOME/.gitconfig" &&
		git config -f "$test_config" branch.autosetuprebase remote &&
		rm -fr dst &&
		git clone src dst &&
		cd dst &&
		actual="z$(git config branch.master.rebase)" &&
		test ztrue = $actual
	)
'

test_expect_success 'respect url-encoding of file://' '
	git init x+y &&
	git clone "file://$PWD/x+y" xy-url-1 &&
	git clone "file://$PWD/x%2By" xy-url-2
'

test_expect_success 'do not query-string-decode + in URLs' '
	rm -rf x+y &&
	git init "x y" &&
	test_must_fail git clone "file://$PWD/x+y" xy-no-plus
'

test_expect_success 'do not respect url-encoding of non-url path' '
	git init x+y &&
	test_must_fail git clone x%2By xy-regular &&
	git clone x+y xy-regular
'

test_expect_success 'clone separate gitdir' '
	rm -rf dst &&
	git clone --separate-git-dir realgitdir src dst &&
	test -d realgitdir/refs
'

test_expect_success 'clone separate gitdir: output' '
	echo "gitdir: `pwd`/realgitdir" >expected &&
	test_cmp expected dst/.git
'

test_expect_success 'clone separate gitdir where target already exists' '
	rm -rf dst &&
	test_must_fail git clone --separate-git-dir realgitdir src dst
'

test_expect_success 'clone --reference from original' '
	git clone --shared --bare src src-1 &&
	git clone --bare src src-2 &&
	git clone --reference=src-2 --bare src-1 target-8 &&
	grep /src-2/ target-8/objects/info/alternates
'

test_expect_success 'clone with more than one --reference' '
	git clone --bare src src-3 &&
	git clone --bare src src-4 &&
	git clone --reference=src-3 --reference=src-4 src target-9 &&
	grep /src-3/ target-9/.git/objects/info/alternates &&
	grep /src-4/ target-9/.git/objects/info/alternates
'

test_expect_success 'clone from original with relative alternate' '
	mkdir nest &&
	git clone --bare src nest/src-5 &&
	echo ../../../src/.git/objects >nest/src-5/objects/info/alternates &&
	git clone --bare nest/src-5 target-10 &&
	grep /src/\\.git/objects target-10/objects/info/alternates
'

test_done
