#!/bin/sh

test_description='fetching and pushing, with or without wildcard'

. ./test-lib.sh

D=`pwd`

mk_empty () {
	rm -fr testrepo &&
	mkdir testrepo &&
	(
		cd testrepo &&
		git init &&
		mv .git/hooks .git/hooks-disabled
	)
}

mk_test () {
	mk_empty &&
	(
		for ref in "$@"
		do
			git push testrepo $the_first_commit:refs/$ref || {
				echo "Oops, push refs/$ref failure"
				exit 1
			}
		done &&
		cd testrepo &&
		for ref in "$@"
		do
			r=$(git show-ref -s --verify refs/$ref) &&
			test "z$r" = "z$the_first_commit" || {
				echo "Oops, refs/$ref is wrong"
				exit 1
			}
		done &&
		git fsck --full
	)
}

check_push_result () {
	(
		cd testrepo &&
		it="$1" &&
		shift
		for ref in "$@"
		do
			r=$(git show-ref -s --verify refs/$ref) &&
			test "z$r" = "z$it" || {
				echo "Oops, refs/$ref is wrong"
				exit 1
			}
		done &&
		git fsck --full
	)
}

test_expect_success setup '

	: >path1 &&
	git add path1 &&
	test_tick &&
	git commit -a -m repo &&
	the_first_commit=$(git show-ref -s --verify refs/heads/master) &&

	: >path2 &&
	git add path2 &&
	test_tick &&
	git commit -a -m second &&
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

test_expect_success 'push with matching heads' '

	mk_test heads/master &&
	git push testrepo &&
	check_push_result $the_commit heads/master

'

test_expect_success 'push with no ambiguity (1)' '

	mk_test heads/master &&
	git push testrepo master:master &&
	check_push_result $the_commit heads/master

'

test_expect_success 'push with no ambiguity (2)' '

	mk_test remotes/origin/master &&
	git push testrepo master:master &&
	check_push_result $the_commit remotes/origin/master

'

test_expect_success 'push with weak ambiguity (1)' '

	mk_test heads/master remotes/origin/master &&
	git push testrepo master:master &&
	check_push_result $the_commit heads/master &&
	check_push_result $the_first_commit remotes/origin/master

'

test_expect_success 'push with weak ambiguity (2)' '

	mk_test heads/master remotes/origin/master remotes/another/master &&
	git push testrepo master:master &&
	check_push_result $the_commit heads/master &&
	check_push_result $the_first_commit remotes/origin/master remotes/another/master

'

test_expect_success 'push with ambiguity (1)' '

	mk_test remotes/origin/master remotes/frotz/master &&
	if git push testrepo master:master
	then
		echo "Oops, should have failed"
		false
	else
		check_push_result $the_first_commit remotes/origin/master remotes/frotz/master
	fi
'

test_expect_success 'push with ambiguity (2)' '

	mk_test heads/frotz tags/frotz &&
	if git push testrepo master:frotz
	then
		echo "Oops, should have failed"
		false
	else
		check_push_result $the_first_commit heads/frotz tags/frotz
	fi

'

test_expect_success 'push with colon-less refspec (1)' '

	mk_test heads/frotz tags/frotz &&
	git branch -f frotz master &&
	git push testrepo frotz &&
	check_push_result $the_commit heads/frotz &&
	check_push_result $the_first_commit tags/frotz

'

test_expect_success 'push with colon-less refspec (2)' '

	mk_test heads/frotz tags/frotz &&
	if git show-ref --verify -q refs/heads/frotz
	then
		git branch -D frotz
	fi &&
	git tag -f frotz &&
	git push testrepo frotz &&
	check_push_result $the_commit tags/frotz &&
	check_push_result $the_first_commit heads/frotz

'

test_expect_success 'push with colon-less refspec (3)' '

	mk_test &&
	if git show-ref --verify -q refs/tags/frotz
	then
		git tag -d frotz
	fi &&
	git branch -f frotz master &&
	git push testrepo frotz &&
	check_push_result $the_commit heads/frotz &&
	test 1 = $( cd testrepo && git show-ref | wc -l )
'

test_expect_success 'push with colon-less refspec (4)' '

	mk_test &&
	if git show-ref --verify -q refs/heads/frotz
	then
		git branch -D frotz
	fi &&
	git tag -f frotz &&
	git push testrepo frotz &&
	check_push_result $the_commit tags/frotz &&
	test 1 = $( cd testrepo && git show-ref | wc -l )

'

test_expect_success 'push with dry-run' '

	mk_test heads/master &&
	(cd testrepo &&
	 old_commit=$(git show-ref -s --verify refs/heads/master)) &&
	git push --dry-run testrepo &&
	check_push_result $old_commit heads/master
'

test_expect_success 'push updates local refs' '

	rm -rf parent child &&
	mkdir parent &&
	(cd parent && git init &&
		echo one >foo && git add foo && git commit -m one) &&
	git clone parent child &&
	(cd child &&
		echo two >foo && git commit -a -m two &&
		git push &&
	test $(git rev-parse master) = $(git rev-parse remotes/origin/master))

'

test_expect_success 'push does not update local refs on failure' '

	rm -rf parent child &&
	mkdir parent &&
	(cd parent && git init &&
		echo one >foo && git add foo && git commit -m one &&
		echo exit 1 >.git/hooks/pre-receive &&
		chmod +x .git/hooks/pre-receive) &&
	git clone parent child &&
	(cd child &&
		echo two >foo && git commit -a -m two &&
		! git push &&
		test $(git rev-parse master) != \
			$(git rev-parse remotes/origin/master))

'

test_expect_success 'allow deleting an invalid remote ref' '

	pwd &&
	rm -f testrepo/.git/objects/??/* &&
	git push testrepo :refs/heads/master &&
	(cd testrepo && ! git rev-parse --verify refs/heads/master)

'

test_done
