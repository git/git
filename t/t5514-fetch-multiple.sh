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
	git clone --mirror two three
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

test_done
