#!/bin/sh

test_description='fetch --all works correctly'

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
	git checkout master
	)
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
  one/master
  one/side
  origin/HEAD -> origin/master
  origin/master
  origin/side
  three/another
  three/master
  three/side
  two/another
  two/master
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
	 test_must_fail git fetch --all origin master)
'

cat > expect << EOF
  origin/HEAD -> origin/master
  origin/master
  origin/side
  three/another
  three/master
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
  one/master
  one/side
  two/another
  two/master
  two/side
EOF

test_expect_success 'git fetch --multiple (two remotes)' '
	(git clone one test4 &&
	 cd test4 &&
	 git remote rm origin &&
	 git remote add one ../one &&
	 git remote add two ../two &&
	 git fetch --multiple one two &&
	 git branch -r > output &&
	 test_cmp ../expect output)
'

test_expect_success 'git fetch --multiple (bad remote names)' '
	(cd test4 &&
	 test_must_fail git fetch --multiple four)
'


test_expect_success 'git fetch --all (skipFetchAll)' '
	(cd test4 &&
	 for b in $(git branch -r)
	 do
		git branch -r -d $b || break
	 done &&
	 git remote add three ../three &&
	 git config remote.three.skipFetchAll true &&
	 git fetch --all &&
	 git branch -r > output &&
	 test_cmp ../expect output)
'

cat > expect << EOF
  one/master
  one/side
  three/another
  three/master
  three/side
  two/another
  two/master
  two/side
EOF

test_expect_success 'git fetch --multiple (ignoring skipFetchAll)' '
	(cd test4 &&
	 for b in $(git branch -r)
	 do
		git branch -r -d $b || break
	 done &&
	 git fetch --multiple one two three &&
	 git branch -r > output &&
	 test_cmp ../expect output)
'

test_expect_success 'git fetch --all --no-tags' '
	>expect &&
	git clone one test5 &&
	git clone test5 test6 &&
	(cd test5 && git tag test-tag) &&
	(
		cd test6 &&
		git fetch --all --no-tags &&
		git tag >output
	) &&
	test_cmp expect test6/output
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

test_done
