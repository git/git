#!/bin/sh

test_description='unpack-trees error messages'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


test_expect_success 'setup' '
	echo one >one &&
	but add one &&
	but cummit -a -m First &&

	but checkout -b branch &&
	echo two >two &&
	echo three >three &&
	echo four >four &&
	echo five >five &&
	but add two three four five &&
	but cummit -m Second &&

	but checkout main &&
	echo other >two &&
	echo other >three &&
	echo other >four &&
	echo other >five
'

cat >expect <<\EOF
error: The following untracked working tree files would be overwritten by merge:
	five
	four
	three
	two
Please move or remove them before you merge.
Aborting
EOF

test_expect_success 'untracked files overwritten by merge (fast and non-fast forward)' '
	test_must_fail but merge branch 2>out &&
	test_cmp out expect &&
	but cummit --allow-empty -m empty &&
	(
		GIT_MERGE_VERBOSITY=0 &&
		export GIT_MERGE_VERBOSITY &&
		test_must_fail but merge branch 2>out2
	) &&
	test_cmp out2 expect &&
	but reset --hard HEAD^
'

cat >expect <<\EOF
error: Your local changes to the following files would be overwritten by merge:
	four
	three
	two
Please cummit your changes or stash them before you merge.
error: The following untracked working tree files would be overwritten by merge:
	five
Please move or remove them before you merge.
Aborting
EOF

test_expect_success 'untracked files or local changes ovewritten by merge' '
	but add two &&
	but add three &&
	but add four &&
	test_must_fail but merge branch 2>out &&
	test_cmp out expect
'

cat >expect <<\EOF
error: Your local changes to the following files would be overwritten by checkout:
	rep/one
	rep/two
Please cummit your changes or stash them before you switch branches.
Aborting
EOF

test_expect_success 'cannot switch branches because of local changes' '
	but add five &&
	mkdir rep &&
	echo one >rep/one &&
	echo two >rep/two &&
	but add rep/one rep/two &&
	but cummit -m Fourth &&
	but checkout main &&
	echo uno >rep/one &&
	echo dos >rep/two &&
	test_must_fail but checkout branch 2>out &&
	test_cmp out expect
'

cat >expect <<\EOF
error: Your local changes to the following files would be overwritten by checkout:
	rep/one
	rep/two
Please cummit your changes or stash them before you switch branches.
Aborting
EOF

test_expect_success 'not uptodate file porcelain checkout error' '
	but add rep/one rep/two &&
	test_must_fail but checkout branch 2>out &&
	test_cmp out expect
'

cat >expect <<\EOF
error: Updating the following directories would lose untracked files in them:
	rep
	rep2

Aborting
EOF

test_expect_success 'not_uptodate_dir porcelain checkout error' '
	but init uptodate &&
	cd uptodate &&
	mkdir rep &&
	mkdir rep2 &&
	touch rep/foo &&
	touch rep2/foo &&
	but add rep/foo rep2/foo &&
	but cummit -m init &&
	but checkout -b branch &&
	but rm rep -r &&
	but rm rep2 -r &&
	>rep &&
	>rep2 &&
	but add rep rep2 &&
	but cummit -m "added test as a file" &&
	but checkout main &&
	>rep/untracked-file &&
	>rep2/untracked-file &&
	test_must_fail but checkout branch 2>out &&
	test_cmp out ../expect
'

test_done
