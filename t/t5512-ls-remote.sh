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

test_expect_success 'confuses pattern as remote when no remote specified' '
	cat >exp <<-\EOF &&
	fatal: '\''refs*master'\'' does not appear to be a git repository
	fatal: Could not read from remote repository.

	Please make sure you have the correct access rights
	and the repository exists.
	EOF
	#
	# Do not expect "git ls-remote <pattern>" to work; ls-remote needs
	# <remote> if you want to feed <pattern>, just like you cannot say
	# fetch <branch>.
	# We could just as easily have used "master"; the "*" emphasizes its
	# role as a pattern.
	test_must_fail git ls-remote refs*master >actual 2>&1 &&
	test_i18ncmp exp actual
'

test_expect_success 'die with non-2 for wrong repository even with --exit-code' '
	{
		git ls-remote --exit-code ./no-such-repository
		status=$?
	} &&
	test $status != 2 && test $status != 0
'

test_expect_success 'Report success even when nothing matches' '
	git ls-remote other.git "refs/nsn/*" >actual &&
	>expect &&
	test_cmp expect actual
'

test_expect_success 'Report no-match with --exit-code' '
	test_expect_code 2 git ls-remote --exit-code other.git "refs/nsn/*" >actual &&
	>expect &&
	test_cmp expect actual
'

test_expect_success 'Report match with --exit-code' '
	git ls-remote --exit-code other.git "refs/tags/*" >actual &&
	git ls-remote . tags/mark >expect &&
	test_cmp expect actual
'

test_expect_success 'set up some extra tags for ref hiding' '
	git tag magic/one &&
	git tag magic/two
'

for configsection in transfer uploadpack
do
	test_expect_success "Hide some refs with $configsection.hiderefs" '
		test_config $configsection.hiderefs refs/tags &&
		git ls-remote . >actual &&
		test_unconfig $configsection.hiderefs &&
		git ls-remote . |
		sed -e "/	refs\/tags\//d" >expect &&
		test_cmp expect actual
	'

	test_expect_success "Override hiding of $configsection.hiderefs" '
		test_when_finished "test_unconfig $configsection.hiderefs" &&
		git config --add $configsection.hiderefs refs/tags &&
		git config --add $configsection.hiderefs "!refs/tags/magic" &&
		git config --add $configsection.hiderefs refs/tags/magic/one &&
		git ls-remote . >actual &&
		grep refs/tags/magic/two actual &&
		! grep refs/tags/magic/one actual
	'

done

test_expect_success 'overrides work between mixed transfer/upload-pack hideRefs' '
	test_config uploadpack.hiderefs refs/tags &&
	test_config transfer.hiderefs "!refs/tags/magic" &&
	git ls-remote . >actual &&
	grep refs/tags/magic actual
'

test_expect_success 'ls-remote --symref' '
	cat >expect <<-\EOF &&
	ref: refs/heads/master	HEAD
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	HEAD
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	refs/heads/master
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	refs/remotes/origin/HEAD
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	refs/remotes/origin/master
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	refs/tags/mark
	EOF
	git ls-remote --symref >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote with filtered symref (refname)' '
	cat >expect <<-\EOF &&
	ref: refs/heads/master	HEAD
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	HEAD
	EOF
	git ls-remote --symref . HEAD >actual &&
	test_cmp expect actual
'

test_expect_failure 'ls-remote with filtered symref (--heads)' '
	git symbolic-ref refs/heads/foo refs/tags/mark &&
	cat >expect <<-\EOF &&
	ref: refs/tags/mark	refs/heads/foo
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	refs/heads/foo
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	refs/heads/master
	EOF
	git ls-remote --symref --heads . >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --symref omits filtered-out matches' '
	cat >expect <<-\EOF &&
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	refs/heads/foo
	1bd44cb9d13204b0fe1958db0082f5028a16eb3a	refs/heads/master
	EOF
	git ls-remote --symref --heads . >actual &&
	test_cmp expect actual &&
	git ls-remote --symref . "refs/heads/*" >actual &&
	test_cmp expect actual
'

test_lazy_prereq GIT_DAEMON '
	test_tristate GIT_TEST_GIT_DAEMON &&
	test "$GIT_TEST_GIT_DAEMON" != false
'

# This test spawns a daemon, so run it only if the user would be OK with
# testing with git-daemon.
test_expect_success PIPE,JGIT,GIT_DAEMON 'indicate no refs in standards-compliant empty remote' '
	JGIT_DAEMON_PORT=${JGIT_DAEMON_PORT-${this_test#t}} &&
	JGIT_DAEMON_PID= &&
	git init --bare empty.git &&
	>empty.git/git-daemon-export-ok &&
	mkfifo jgit_daemon_output &&
	{
		jgit daemon --port="$JGIT_DAEMON_PORT" . >jgit_daemon_output &
		JGIT_DAEMON_PID=$!
	} &&
	test_when_finished kill "$JGIT_DAEMON_PID" &&
	{
		read line &&
		case $line in
		Exporting*)
			;;
		*)
			echo "Expected: Exporting" &&
			false;;
		esac &&
		read line &&
		case $line in
		"Listening on"*)
			;;
		*)
			echo "Expected: Listening on" &&
			false;;
		esac
	} <jgit_daemon_output &&
	# --exit-code asks the command to exit with 2 when no
	# matching refs are found.
	test_expect_code 2 git ls-remote --exit-code git://localhost:$JGIT_DAEMON_PORT/empty.git
'

test_done
