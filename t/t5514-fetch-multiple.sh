#!/bin/sh

test_description='fetch --all works correctly'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

setup_repository () {
	mkdir "$1" && (
	cd "$1" &&
	git init &&
	>file &&
	git add file &&
	test_tick &&
	git commit -m "Initial" &&
	git checkout -b side &&
	>elif &&
	git add elif &&
	test_tick &&
	git commit -m "Second" &&
	git checkout main
	)
}

setup_test_clone () {
	test_dir="$1" &&
	git clone one "$test_dir" &&
	for r in one two three
	do
		git -C "$test_dir" remote add "$r" "../$r" || return 1
	done
}

test_expect_success setup '
	setup_repository one &&
	setup_repository two &&
	(
		cd two && git branch another
	) &&
	git clone --mirror two three &&
	git clone one test
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

test_expect_success 'git fetch --all' '
	(cd test &&
	 git remote add one ../one &&
	 git remote add two ../two &&
	 git remote add three ../three &&
	 git fetch --all &&
	 git branch -r > output &&
	 test_cmp expect output)
'

test_expect_success 'git fetch --all --no-write-fetch-head' '
	(cd test &&
	rm -f .git/FETCH_HEAD &&
	git fetch --all --no-write-fetch-head &&
	test_path_is_missing .git/FETCH_HEAD)
'

test_expect_success 'git fetch --all should continue if a remote has errors' '
	(git clone one test2 &&
	 cd test2 &&
	 git remote add bad ../non-existing &&
	 git remote add one ../one &&
	 git remote add two ../two &&
	 git remote add three ../three &&
	 test_must_fail git fetch --all &&
	 git branch -r > output &&
	 test_cmp ../test/expect output)
'

test_expect_success 'git fetch --all does not allow non-option arguments' '
	(cd test &&
	 test_must_fail git fetch --all origin &&
	 test_must_fail git fetch --all origin main)
'

cat > expect << EOF
  origin/HEAD -> origin/main
  origin/main
  origin/side
  three/another
  three/main
  three/side
EOF

test_expect_success 'git fetch --multiple (but only one remote)' '
	(git clone one test3 &&
	 cd test3 &&
	 git remote add three ../three &&
	 git fetch --multiple three &&
	 git branch -r > output &&
	 test_cmp ../expect output)
'

cat > expect << EOF
  one/main
  one/side
  two/another
  two/main
  two/side
EOF

test_expect_success 'git fetch --multiple (two remotes)' '
	(git clone one test4 &&
	 cd test4 &&
	 git remote rm origin &&
	 git remote add one ../one &&
	 git remote add two ../two &&
	 GIT_TRACE=1 git fetch --multiple one two 2>trace &&
	 git branch -r > output &&
	 test_cmp ../expect output &&
	 grep "built-in: git maintenance" trace >gc &&
	 test_line_count = 1 gc
	)
'

test_expect_success 'git fetch --multiple (bad remote names)' '
	(cd test4 &&
	 test_must_fail git fetch --multiple four)
'


test_expect_success 'git fetch --all (skipFetchAll)' '
	(cd test4 &&
	 for b in $(git branch -r)
	 do
		git branch -r -d $b || exit 1
	 done &&
	 git remote add three ../three &&
	 git config remote.three.skipFetchAll true &&
	 git fetch --all &&
	 git branch -r > output &&
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

test_expect_success 'git fetch --multiple (ignoring skipFetchAll)' '
	(cd test4 &&
	 for b in $(git branch -r)
	 do
		git branch -r -d $b || exit 1
	 done &&
	 git fetch --multiple one two three &&
	 git branch -r > output &&
	 test_cmp ../expect output)
'

test_expect_success 'git fetch --all --no-tags' '
	git clone one test5 &&
	git clone test5 test6 &&
	(cd test5 && git tag test-tag) &&
	(
		cd test6 &&
		git fetch --all --no-tags &&
		git tag >output
	) &&
	test_must_be_empty test6/output
'

test_expect_success 'git fetch --all --tags' '
	echo test-tag >expect &&
	git clone one test7 &&
	git clone test7 test8 &&
	(
		cd test7 &&
		test_commit test-tag &&
		git reset --hard HEAD^
	) &&
	(
		cd test8 &&
		git fetch --all --tags &&
		git tag >output
	) &&
	test_cmp expect test8/output
'

test_expect_success 'parallel' '
	git remote add one ./bogus1 &&
	git remote add two ./bogus2 &&

	test_must_fail env GIT_TRACE="$PWD/trace" \
		git fetch --jobs=2 --multiple one two 2>err &&
	grep "preparing to run up to 2 tasks" trace &&
	test_grep "could not fetch .one.*128" err &&
	test_grep "could not fetch .two.*128" err
'

test_expect_success 'git fetch --multiple --jobs=0 picks a default' '
	(cd test &&
	 git fetch --multiple --jobs=0)
'

create_fetch_all_expect () {
	cat >expect <<-\EOF
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
}

for fetch_all in true false
do
	test_expect_success "git fetch --all (works with fetch.all = $fetch_all)" '
		test_dir="test_fetch_all_$fetch_all" &&
		setup_test_clone "$test_dir" &&
		(
			cd "$test_dir" &&
			git config fetch.all $fetch_all &&
			git fetch --all &&
			create_fetch_all_expect &&
			git branch -r >actual &&
			test_cmp expect actual
		)
	'
done

test_expect_success 'git fetch (fetch all remotes with fetch.all = true)' '
	setup_test_clone test9 &&
	(
		cd test9 &&
		git config fetch.all true &&
		git fetch &&
		git branch -r >actual &&
		create_fetch_all_expect &&
		test_cmp expect actual
	)
'

create_fetch_one_expect () {
	cat >expect <<-\EOF
	  one/main
	  one/side
	  origin/HEAD -> origin/main
	  origin/main
	  origin/side
	EOF
}

test_expect_success 'git fetch one (explicit remote overrides fetch.all)' '
	setup_test_clone test10 &&
	(
		cd test10 &&
		git config fetch.all true &&
		git fetch one &&
		create_fetch_one_expect &&
		git branch -r >actual &&
		test_cmp expect actual
	)
'

create_fetch_two_as_origin_expect () {
	cat >expect <<-\EOF
	  origin/HEAD -> origin/main
	  origin/another
	  origin/main
	  origin/side
	EOF
}

test_expect_success 'git config fetch.all false (fetch only default remote)' '
	setup_test_clone test11 &&
	(
		cd test11 &&
		git config fetch.all false &&
		git remote set-url origin ../two &&
		git fetch &&
		create_fetch_two_as_origin_expect &&
		git branch -r >actual &&
		test_cmp expect actual
	)
'

for fetch_all in true false
do
	test_expect_success "git fetch --no-all (fetch only default remote with fetch.all = $fetch_all)" '
		test_dir="test_no_all_fetch_all_$fetch_all" &&
		setup_test_clone "$test_dir" &&
		(
			cd "$test_dir" &&
			git config fetch.all $fetch_all &&
			git remote set-url origin ../two &&
			git fetch --no-all &&
			create_fetch_two_as_origin_expect &&
			git branch -r >actual &&
			test_cmp expect actual
		)
	'
done

test_expect_success 'git fetch --no-all (fetch only default remote without fetch.all)' '
	setup_test_clone test12 &&
	(
		cd test12 &&
		git config --unset-all fetch.all || true &&
		git remote set-url origin ../two &&
		git fetch --no-all &&
		create_fetch_two_as_origin_expect &&
		git branch -r >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'git fetch --all --no-all (fetch only default remote)' '
	setup_test_clone test13 &&
	(
		cd test13 &&
		git remote set-url origin ../two &&
		git fetch --all --no-all &&
		create_fetch_two_as_origin_expect &&
		git branch -r >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'git fetch --no-all one (fetch only explicit remote)' '
	setup_test_clone test14 &&
	(
		cd test14 &&
		git fetch --no-all one &&
		create_fetch_one_expect &&
		git branch -r >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'git fetch --no-all --all (fetch all remotes)' '
	setup_test_clone test15 &&
	(
		cd test15 &&
		git fetch --no-all --all &&
		create_fetch_all_expect &&
		git branch -r >actual &&
		test_cmp expect actual
	)
'

test_done
