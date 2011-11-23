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
		git config receive.denyCurrentBranch warn &&
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

mk_child() {
	rm -rf "$1" &&
	git clone testrepo "$1"
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

	>path1 &&
	git add path1 &&
	test_tick &&
	git commit -a -m repo &&
	the_first_commit=$(git show-ref -s --verify refs/heads/master) &&

	>path2 &&
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

test_expect_success 'fetch with insteadOf' '
	mk_empty &&
	(
		TRASH=$(pwd)/ &&
		cd testrepo &&
		git config "url.$TRASH.insteadOf" trash/ &&
		git config remote.up.url trash/. &&
		git config remote.up.fetch "refs/heads/*:refs/remotes/origin/*" &&
		git fetch up &&

		r=$(git show-ref -s --verify refs/remotes/origin/master) &&
		test "z$r" = "z$the_commit" &&

		test 1 = $(git for-each-ref refs/remotes/origin | wc -l)
	)
'

test_expect_success 'fetch with pushInsteadOf (should not rewrite)' '
	mk_empty &&
	(
		TRASH=$(pwd)/ &&
		cd testrepo &&
		git config "url.trash/.pushInsteadOf" "$TRASH" &&
		git config remote.up.url "$TRASH." &&
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

test_expect_success 'push with insteadOf' '
	mk_empty &&
	TRASH="$(pwd)/" &&
	git config "url.$TRASH.insteadOf" trash/ &&
	git push trash/testrepo refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		r=$(git show-ref -s --verify refs/remotes/origin/master) &&
		test "z$r" = "z$the_commit" &&

		test 1 = $(git for-each-ref refs/remotes/origin | wc -l)
	)
'

test_expect_success 'push with pushInsteadOf' '
	mk_empty &&
	TRASH="$(pwd)/" &&
	git config "url.$TRASH.pushInsteadOf" trash/ &&
	git push trash/testrepo refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		r=$(git show-ref -s --verify refs/remotes/origin/master) &&
		test "z$r" = "z$the_commit" &&

		test 1 = $(git for-each-ref refs/remotes/origin | wc -l)
	)
'

test_expect_success 'push with pushInsteadOf and explicit pushurl (pushInsteadOf should not rewrite)' '
	mk_empty &&
	TRASH="$(pwd)/" &&
	git config "url.trash2/.pushInsteadOf" trash/ &&
	git config remote.r.url trash/wrong &&
	git config remote.r.pushurl "$TRASH/testrepo" &&
	git push r refs/heads/master:refs/remotes/origin/master &&
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

test_expect_success 'push with matching heads on the command line' '

	mk_test heads/master &&
	git push testrepo : &&
	check_push_result $the_commit heads/master

'

test_expect_success 'failed (non-fast-forward) push with matching heads' '

	mk_test heads/master &&
	git push testrepo : &&
	git commit --amend -massaged &&
	test_must_fail git push testrepo &&
	check_push_result $the_commit heads/master &&
	git reset --hard $the_commit

'

test_expect_success 'push --force with matching heads' '

	mk_test heads/master &&
	git push testrepo : &&
	git commit --amend -massaged &&
	git push --force testrepo &&
	! check_push_result $the_commit heads/master &&
	git reset --hard $the_commit

'

test_expect_success 'push with matching heads and forced update' '

	mk_test heads/master &&
	git push testrepo : &&
	git commit --amend -massaged &&
	git push testrepo +: &&
	! check_push_result $the_commit heads/master &&
	git reset --hard $the_commit

'

test_expect_success 'push with no ambiguity (1)' '

	mk_test heads/master &&
	git push testrepo master:master &&
	check_push_result $the_commit heads/master

'

test_expect_success 'push with no ambiguity (2)' '

	mk_test remotes/origin/master &&
	git push testrepo master:origin/master &&
	check_push_result $the_commit remotes/origin/master

'

test_expect_success 'push with colon-less refspec, no ambiguity' '

	mk_test heads/master heads/t/master &&
	git branch -f t/master master &&
	git push testrepo master &&
	check_push_result $the_commit heads/master &&
	check_push_result $the_first_commit heads/t/master

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

test_expect_success 'push with ambiguity' '

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

test_expect_success 'push head with non-existent, incomplete dest' '

	mk_test &&
	git push testrepo master:branch &&
	check_push_result $the_commit heads/branch

'

test_expect_success 'push tag with non-existent, incomplete dest' '

	mk_test &&
	git tag -f v1.0 &&
	git push testrepo v1.0:tag &&
	check_push_result $the_commit tags/tag

'

test_expect_success 'push sha1 with non-existent, incomplete dest' '

	mk_test &&
	test_must_fail git push testrepo `git rev-parse master`:foo

'

test_expect_success 'push ref expression with non-existent, incomplete dest' '

	mk_test &&
	test_must_fail git push testrepo master^:branch

'

test_expect_success 'push with HEAD' '

	mk_test heads/master &&
	git checkout master &&
	git push testrepo HEAD &&
	check_push_result $the_commit heads/master

'

test_expect_success 'push with HEAD nonexisting at remote' '

	mk_test heads/master &&
	git checkout -b local master &&
	git push testrepo HEAD &&
	check_push_result $the_commit heads/local
'

test_expect_success 'push with +HEAD' '

	mk_test heads/master &&
	git checkout master &&
	git branch -D local &&
	git checkout -b local &&
	git push testrepo master local &&
	check_push_result $the_commit heads/master &&
	check_push_result $the_commit heads/local &&

	# Without force rewinding should fail
	git reset --hard HEAD^ &&
	test_must_fail git push testrepo HEAD &&
	check_push_result $the_commit heads/local &&

	# With force rewinding should succeed
	git push testrepo +HEAD &&
	check_push_result $the_first_commit heads/local

'

test_expect_success 'push HEAD with non-existent, incomplete dest' '

	mk_test &&
	git checkout master &&
	git push testrepo HEAD:branch &&
	check_push_result $the_commit heads/branch

'

test_expect_success 'push with config remote.*.push = HEAD' '

	mk_test heads/local &&
	git checkout master &&
	git branch -f local $the_commit &&
	(
		cd testrepo &&
		git checkout local &&
		git reset --hard $the_first_commit
	) &&
	git config remote.there.url testrepo &&
	git config remote.there.push HEAD &&
	git config branch.master.remote there &&
	git push &&
	check_push_result $the_commit heads/master &&
	check_push_result $the_first_commit heads/local
'

# clean up the cruft left with the previous one
git config --remove-section remote.there
git config --remove-section branch.master

test_expect_success 'push with config remote.*.pushurl' '

	mk_test heads/master &&
	git checkout master &&
	git config remote.there.url test2repo &&
	git config remote.there.pushurl testrepo &&
	git push there &&
	check_push_result $the_commit heads/master
'

# clean up the cruft left with the previous one
git config --remove-section remote.there

test_expect_success 'push with dry-run' '

	mk_test heads/master &&
	(
		cd testrepo &&
		old_commit=$(git show-ref -s --verify refs/heads/master)
	) &&
	git push --dry-run testrepo &&
	check_push_result $old_commit heads/master
'

test_expect_success 'push updates local refs' '

	mk_test heads/master &&
	mk_child child &&
	(
		cd child &&
		git pull .. master &&
		git push &&
		test $(git rev-parse master) = \
			$(git rev-parse remotes/origin/master)
	)

'

test_expect_success 'push updates up-to-date local refs' '

	mk_test heads/master &&
	mk_child child1 &&
	mk_child child2 &&
	(cd child1 && git pull .. master && git push) &&
	(
		cd child2 &&
		git pull ../child1 master &&
		git push &&
		test $(git rev-parse master) = \
			$(git rev-parse remotes/origin/master)
	)

'

test_expect_success 'push preserves up-to-date packed refs' '

	mk_test heads/master &&
	mk_child child &&
	(
		cd child &&
		git push &&
		! test -f .git/refs/remotes/origin/master
	)

'

test_expect_success 'push does not update local refs on failure' '

	mk_test heads/master &&
	mk_child child &&
	mkdir testrepo/.git/hooks &&
	echo "#!/no/frobnication/today" >testrepo/.git/hooks/pre-receive &&
	chmod +x testrepo/.git/hooks/pre-receive &&
	(
		cd child &&
		git pull .. master
		test_must_fail git push &&
		test $(git rev-parse master) != \
			$(git rev-parse remotes/origin/master)
	)

'

test_expect_success 'allow deleting an invalid remote ref' '

	mk_test heads/master &&
	rm -f testrepo/.git/objects/??/* &&
	git push testrepo :refs/heads/master &&
	(cd testrepo && test_must_fail git rev-parse --verify refs/heads/master)

'

test_expect_success 'allow deleting a ref using --delete' '
	mk_test heads/master &&
	(cd testrepo && git config receive.denyDeleteCurrent warn) &&
	git push testrepo --delete master &&
	(cd testrepo && test_must_fail git rev-parse --verify refs/heads/master)
'

test_expect_success 'allow deleting a tag using --delete' '
	mk_test heads/master &&
	git tag -a -m dummy_message deltag heads/master &&
	git push testrepo --tags &&
	(cd testrepo && git rev-parse --verify -q refs/tags/deltag) &&
	git push testrepo --delete tag deltag &&
	(cd testrepo && test_must_fail git rev-parse --verify refs/tags/deltag)
'

test_expect_success 'push --delete without args aborts' '
	mk_test heads/master &&
	test_must_fail git push testrepo --delete
'

test_expect_success 'push --delete refuses src:dest refspecs' '
	mk_test heads/master &&
	test_must_fail git push testrepo --delete master:foo
'

test_expect_success 'warn on push to HEAD of non-bare repository' '
	mk_test heads/master &&
	(
		cd testrepo &&
		git checkout master &&
		git config receive.denyCurrentBranch warn
	) &&
	git push testrepo master 2>stderr &&
	grep "warning: updating the current branch" stderr
'

test_expect_success 'deny push to HEAD of non-bare repository' '
	mk_test heads/master &&
	(
		cd testrepo &&
		git checkout master &&
		git config receive.denyCurrentBranch true
	) &&
	test_must_fail git push testrepo master
'

test_expect_success 'allow push to HEAD of bare repository (bare)' '
	mk_test heads/master &&
	(
		cd testrepo &&
		git checkout master &&
		git config receive.denyCurrentBranch true &&
		git config core.bare true
	) &&
	git push testrepo master 2>stderr &&
	! grep "warning: updating the current branch" stderr
'

test_expect_success 'allow push to HEAD of non-bare repository (config)' '
	mk_test heads/master &&
	(
		cd testrepo &&
		git checkout master &&
		git config receive.denyCurrentBranch false
	) &&
	git push testrepo master 2>stderr &&
	! grep "warning: updating the current branch" stderr
'

test_expect_success 'fetch with branches' '
	mk_empty &&
	git branch second $the_first_commit &&
	git checkout second &&
	echo ".." > testrepo/.git/branches/branch1 &&
	(
		cd testrepo &&
		git fetch branch1 &&
		r=$(git show-ref -s --verify refs/heads/branch1) &&
		test "z$r" = "z$the_commit" &&
		test 1 = $(git for-each-ref refs/heads | wc -l)
	) &&
	git checkout master
'

test_expect_success 'fetch with branches containing #' '
	mk_empty &&
	echo "..#second" > testrepo/.git/branches/branch2 &&
	(
		cd testrepo &&
		git fetch branch2 &&
		r=$(git show-ref -s --verify refs/heads/branch2) &&
		test "z$r" = "z$the_first_commit" &&
		test 1 = $(git for-each-ref refs/heads | wc -l)
	) &&
	git checkout master
'

test_expect_success 'push with branches' '
	mk_empty &&
	git checkout second &&
	echo "testrepo" > .git/branches/branch1 &&
	git push branch1 &&
	(
		cd testrepo &&
		r=$(git show-ref -s --verify refs/heads/master) &&
		test "z$r" = "z$the_first_commit" &&
		test 1 = $(git for-each-ref refs/heads | wc -l)
	)
'

test_expect_success 'push with branches containing #' '
	mk_empty &&
	echo "testrepo#branch3" > .git/branches/branch2 &&
	git push branch2 &&
	(
		cd testrepo &&
		r=$(git show-ref -s --verify refs/heads/branch3) &&
		test "z$r" = "z$the_first_commit" &&
		test 1 = $(git for-each-ref refs/heads | wc -l)
	) &&
	git checkout master
'

test_expect_success 'push into aliased refs (consistent)' '
	mk_test heads/master &&
	mk_child child1 &&
	mk_child child2 &&
	(
		cd child1 &&
		git branch foo &&
		git symbolic-ref refs/heads/bar refs/heads/foo
		git config receive.denyCurrentBranch false
	) &&
	(
		cd child2 &&
		>path2 &&
		git add path2 &&
		test_tick &&
		git commit -a -m child2 &&
		git branch foo &&
		git branch bar &&
		git push ../child1 foo bar
	)
'

test_expect_success 'push into aliased refs (inconsistent)' '
	mk_test heads/master &&
	mk_child child1 &&
	mk_child child2 &&
	(
		cd child1 &&
		git branch foo &&
		git symbolic-ref refs/heads/bar refs/heads/foo
		git config receive.denyCurrentBranch false
	) &&
	(
		cd child2 &&
		>path2 &&
		git add path2 &&
		test_tick &&
		git commit -a -m child2 &&
		git branch foo &&
		>path3 &&
		git add path3 &&
		test_tick &&
		git commit -a -m child2 &&
		git branch bar &&
		test_must_fail git push ../child1 foo bar 2>stderr &&
		grep "refusing inconsistent update" stderr
	)
'

test_expect_success 'push --porcelain' '
	mk_empty &&
	echo >.git/foo  "To testrepo" &&
	echo >>.git/foo "*	refs/heads/master:refs/remotes/origin/master	[new branch]"  &&
	echo >>.git/foo "Done" &&
	git push >.git/bar --porcelain  testrepo refs/heads/master:refs/remotes/origin/master &&
	(
		cd testrepo &&
		r=$(git show-ref -s --verify refs/remotes/origin/master) &&
		test "z$r" = "z$the_commit" &&
		test 1 = $(git for-each-ref refs/remotes/origin | wc -l)
	) &&
	test_cmp .git/foo .git/bar
'

test_expect_success 'push --porcelain bad url' '
	mk_empty &&
	test_must_fail git push >.git/bar --porcelain asdfasdfasd refs/heads/master:refs/remotes/origin/master &&
	test_must_fail grep -q Done .git/bar
'

test_expect_success 'push --porcelain rejected' '
	mk_empty &&
	git push testrepo refs/heads/master:refs/remotes/origin/master &&
	(cd testrepo &&
		git reset --hard origin/master^
		git config receive.denyCurrentBranch true) &&

	echo >.git/foo  "To testrepo"  &&
	echo >>.git/foo "!	refs/heads/master:refs/heads/master	[remote rejected] (branch is currently checked out)" &&

	test_must_fail git push >.git/bar --porcelain  testrepo refs/heads/master:refs/heads/master &&
	test_cmp .git/foo .git/bar
'

test_expect_success 'push --porcelain --dry-run rejected' '
	mk_empty &&
	git push testrepo refs/heads/master:refs/remotes/origin/master &&
	(cd testrepo &&
		git reset --hard origin/master
		git config receive.denyCurrentBranch true) &&

	echo >.git/foo  "To testrepo"  &&
	echo >>.git/foo "!	refs/heads/master^:refs/heads/master	[rejected] (non-fast-forward)" &&
	echo >>.git/foo "Done" &&

	test_must_fail git push >.git/bar --porcelain  --dry-run testrepo refs/heads/master^:refs/heads/master &&
	test_cmp .git/foo .git/bar
'

test_done
