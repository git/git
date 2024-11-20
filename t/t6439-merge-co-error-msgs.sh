#!/bin/sh

test_description='unpack-trees error messages'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh


test_expect_success 'setup' '
	echo one >one &&
	git add one &&
	git commit -a -m First &&

	git checkout -b branch &&
	echo two >two &&
	echo three >three &&
	echo four >four &&
	echo five >five &&
	git add two three four five &&
	git commit -m Second &&

	git checkout main &&
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
	test_must_fail git merge branch 2>out &&
	test_cmp out expect &&
	git commit --allow-empty -m empty &&
	(
		GIT_MERGE_VERBOSITY=0 &&
		export GIT_MERGE_VERBOSITY &&
		test_must_fail git merge branch 2>out2
	) &&
	echo "Merge with strategy ${GIT_TEST_MERGE_ALGORITHM:-ort} failed." >>expect &&
	test_cmp out2 expect &&
	git reset --hard HEAD^
'

cat >expect <<\EOF
error: Your local changes to the following files would be overwritten by merge:
	four
	three
	two
Please commit your changes or stash them before you merge.
error: The following untracked working tree files would be overwritten by merge:
	five
Please move or remove them before you merge.
Aborting
EOF

test_expect_success 'untracked files or local changes overwritten by merge' '
	git add two &&
	git add three &&
	git add four &&
	test_must_fail git merge branch 2>out &&
	test_cmp out expect
'

cat >expect <<\EOF
error: Your local changes to the following files would be overwritten by checkout:
	rep/one
	rep/two
Please commit your changes or stash them before you switch branches.
Aborting
EOF

test_expect_success 'cannot switch branches because of local changes' '
	git add five &&
	mkdir rep &&
	echo one >rep/one &&
	echo two >rep/two &&
	git add rep/one rep/two &&
	git commit -m Fourth &&
	git checkout main &&
	echo uno >rep/one &&
	echo dos >rep/two &&
	test_must_fail git checkout branch 2>out &&
	test_cmp out expect
'

cat >expect <<\EOF
error: Your local changes to the following files would be overwritten by checkout:
	rep/one
	rep/two
Please commit your changes or stash them before you switch branches.
Aborting
EOF

test_expect_success 'not uptodate file porcelain checkout error' '
	git add rep/one rep/two &&
	test_must_fail git checkout branch 2>out &&
	test_cmp out expect
'

cat >expect <<\EOF
error: Updating the following directories would lose untracked files in them:
	rep
	rep2

Aborting
EOF

test_expect_success 'not_uptodate_dir porcelain checkout error' '
	git init uptodate &&
	cd uptodate &&
	mkdir rep &&
	mkdir rep2 &&
	touch rep/foo &&
	touch rep2/foo &&
	git add rep/foo rep2/foo &&
	git commit -m init &&
	git checkout -b branch &&
	git rm rep -r &&
	git rm rep2 -r &&
	>rep &&
	>rep2 &&
	git add rep rep2 &&
	git commit -m "added test as a file" &&
	git checkout main &&
	>rep/untracked-file &&
	>rep2/untracked-file &&
	test_must_fail git checkout branch 2>out &&
	test_cmp out ../expect
'

test_done
