#!/bin/sh

test_description='fetch --all works correctly'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

setup_repository () {
	mkdir "$1" && (
	cd "$1" &&
	but init &&
	>file &&
	but add file &&
	test_tick &&
	but cummit -m "Initial" &&
	but checkout -b side &&
	>elif &&
	but add elif &&
	test_tick &&
	but cummit -m "Second" &&
	but checkout main
	)
}

test_expect_success setup '
	setup_repository one &&
	setup_repository two &&
	(
		cd two && but branch another
	) &&
	but clone --mirror two three &&
	but clone one test
'

cat > test/expect << EOF
  one/main
  one/side
  origin/HEAD -> origin/main
  origin/main
  origin/side
  three/another
  three/main
  three/side
  two/another
  two/main
  two/side
EOF

test_expect_success 'but fetch --all' '
	(cd test &&
	 but remote add one ../one &&
	 but remote add two ../two &&
	 but remote add three ../three &&
	 but fetch --all &&
	 but branch -r > output &&
	 test_cmp expect output)
'

test_expect_success 'but fetch --all should continue if a remote has errors' '
	(but clone one test2 &&
	 cd test2 &&
	 but remote add bad ../non-existing &&
	 but remote add one ../one &&
	 but remote add two ../two &&
	 but remote add three ../three &&
	 test_must_fail but fetch --all &&
	 but branch -r > output &&
	 test_cmp ../test/expect output)
'

test_expect_success 'but fetch --all does not allow non-option arguments' '
	(cd test &&
	 test_must_fail but fetch --all origin &&
	 test_must_fail but fetch --all origin main)
'

cat > expect << EOF
  origin/HEAD -> origin/main
  origin/main
  origin/side
  three/another
  three/main
  three/side
EOF

test_expect_success 'but fetch --multiple (but only one remote)' '
	(but clone one test3 &&
	 cd test3 &&
	 but remote add three ../three &&
	 but fetch --multiple three &&
	 but branch -r > output &&
	 test_cmp ../expect output)
'

cat > expect << EOF
  one/main
  one/side
  two/another
  two/main
  two/side
EOF

test_expect_success 'but fetch --multiple (two remotes)' '
	(but clone one test4 &&
	 cd test4 &&
	 but remote rm origin &&
	 but remote add one ../one &&
	 but remote add two ../two &&
	 GIT_TRACE=1 but fetch --multiple one two 2>trace &&
	 but branch -r > output &&
	 test_cmp ../expect output &&
	 grep "built-in: but maintenance" trace >gc &&
	 test_line_count = 1 gc
	)
'

test_expect_success 'but fetch --multiple (bad remote names)' '
	(cd test4 &&
	 test_must_fail but fetch --multiple four)
'


test_expect_success 'but fetch --all (skipFetchAll)' '
	(cd test4 &&
	 for b in $(but branch -r)
	 do
		but branch -r -d $b || exit 1
	 done &&
	 but remote add three ../three &&
	 but config remote.three.skipFetchAll true &&
	 but fetch --all &&
	 but branch -r > output &&
	 test_cmp ../expect output)
'

cat > expect << EOF
  one/main
  one/side
  three/another
  three/main
  three/side
  two/another
  two/main
  two/side
EOF

test_expect_success 'but fetch --multiple (ignoring skipFetchAll)' '
	(cd test4 &&
	 for b in $(but branch -r)
	 do
		but branch -r -d $b || exit 1
	 done &&
	 but fetch --multiple one two three &&
	 but branch -r > output &&
	 test_cmp ../expect output)
'

test_expect_success 'but fetch --all --no-tags' '
	but clone one test5 &&
	but clone test5 test6 &&
	(cd test5 && but tag test-tag) &&
	(
		cd test6 &&
		but fetch --all --no-tags &&
		but tag >output
	) &&
	test_must_be_empty test6/output
'

test_expect_success 'but fetch --all --tags' '
	echo test-tag >expect &&
	but clone one test7 &&
	but clone test7 test8 &&
	(
		cd test7 &&
		test_cummit test-tag &&
		but reset --hard HEAD^
	) &&
	(
		cd test8 &&
		but fetch --all --tags &&
		but tag >output
	) &&
	test_cmp expect test8/output
'

test_expect_success 'parallel' '
	but remote add one ./bogus1 &&
	but remote add two ./bogus2 &&

	test_must_fail env GIT_TRACE="$PWD/trace" \
		but fetch --jobs=2 --multiple one two 2>err &&
	grep "preparing to run up to 2 tasks" trace &&
	test_i18ngrep "could not fetch .one.*128" err &&
	test_i18ngrep "could not fetch .two.*128" err
'

test_done
