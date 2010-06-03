#!/bin/sh

test_description='git ls-remote'

. ./test-lib.sh

test_expect_success setup '

	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag mark &&
	git show-ref --tags -d | sed -e "s/ /	/" >expected.tag &&
	(
		echo "$(git rev-parse HEAD)	HEAD"
		git show-ref -d	| sed -e "s/ /	/"
	) >expected.all &&

	git remote add self "$(pwd)/.git"

'

test_expect_success 'ls-remote --tags .git' '

	git ls-remote --tags .git >actual &&
	test_cmp expected.tag actual

'

test_expect_success 'ls-remote .git' '

	git ls-remote .git >actual &&
	test_cmp expected.all actual

'

test_expect_success 'ls-remote --tags self' '

	git ls-remote --tags self >actual &&
	test_cmp expected.tag actual

'

test_expect_success 'ls-remote self' '

	git ls-remote self >actual &&
	test_cmp expected.all actual

'

test_expect_success 'dies when no remote specified and no default remotes found' '

	test_must_fail git ls-remote

'

test_expect_success 'use "origin" when no remote specified' '

	URL="$(pwd)/.git" &&
	echo "From $URL" >exp_err &&

	git remote add origin "$URL" &&
	git ls-remote 2>actual_err >actual &&

	test_cmp exp_err actual_err &&
	test_cmp expected.all actual

'

test_expect_success 'suppress "From <url>" with -q' '

	git ls-remote -q 2>actual_err &&
	test_must_fail test_cmp exp_err actual_err

'

test_expect_success 'use branch.<name>.remote if possible' '

	#
	# Test that we are indeed using branch.<name>.remote, not "origin", even
	# though the "origin" remote has been set.
	#

	# setup a new remote to differentiate from "origin"
	git clone . other.git &&
	(
		cd other.git &&
		echo "$(git rev-parse HEAD)	HEAD"
		git show-ref	| sed -e "s/ /	/"
	) >exp &&

	URL="other.git" &&
	echo "From $URL" >exp_err &&

	git remote add other $URL &&
	git config branch.master.remote other &&

	git ls-remote 2>actual_err >actual &&
	test_cmp exp_err actual_err &&
	test_cmp exp actual

'

cat >exp <<EOF
fatal: 'refs*master' does not appear to be a git repository
fatal: The remote end hung up unexpectedly
EOF
test_expect_success 'confuses pattern as remote when no remote specified' '
	#
	# Do not expect "git ls-remote <pattern>" to work; ls-remote, correctly,
	# confuses <pattern> for <remote>. Although ugly, this behaviour is akin
	# to the confusion of refspecs for remotes by git-fetch and git-push,
	# eg:
	#
	#   $ git fetch branch
	#

	# We could just as easily have used "master"; the "*" emphasizes its
	# role as a pattern.
	test_must_fail git ls-remote refs*master >actual 2>&1 &&
	test_cmp exp actual

'

test_done
