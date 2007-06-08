#!/bin/sh

test_description='fetching and pushing, with or without wildcard'

. ./test-lib.sh

D=`pwd`

mk_empty () {
	rm -fr testrepo &&
	mkdir testrepo &&
	(
		cd testrepo &&
		git init
	)
}

test_expect_success setup '

	: >path1 &&
	git add path1 &&
	test_tick &&
	git commit -a -m repo &&
	the_commit=$(git show-ref -s --verify refs/heads/master)

'

test_expect_success 'fetch without wildcard' '
	mk_empty &&
	(
		cd testrepo &&
		git fetch .. refs/heads/master:refs/remotes/origin/master &&

		r=$(git show-ref -s --verify refs/remotes/origin/master) &&
		test "z$r" = "z$the_commit" &&

		test 1 = $(git for-each-ref refs/remotes/origin | wc -l)
	)
'

test_expect_success 'fetch with wildcard' '
	mk_empty &&
	(
		cd testrepo &&
		git config remote.up.url .. &&
		git config remote.up.fetch "refs/heads/*:refs/remotes/origin/*" &&
		git fetch up &&

		r=$(git show-ref -s --verify refs/remotes/origin/master) &&
		test "z$r" = "z$the_commit" &&

		test 1 = $(git for-each-ref refs/remotes/origin | wc -l)
	)
'

test_expect_success 'push without wildcard' '
	mk_empty &&

	git push testrepo refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		r=$(git show-ref -s --verify refs/remotes/origin/master) &&
		test "z$r" = "z$the_commit" &&

		test 1 = $(git for-each-ref refs/remotes/origin | wc -l)
	)
'

test_expect_success 'push with wildcard' '
	mk_empty &&

	git push testrepo "refs/heads/*:refs/remotes/origin/*" &&
	(
		cd testrepo &&
		r=$(git show-ref -s --verify refs/remotes/origin/master) &&
		test "z$r" = "z$the_commit" &&

		test 1 = $(git for-each-ref refs/remotes/origin | wc -l)
	)
'

test_done
