#!/bin/sh

test_description='git ls-remote'

. ./test-lib.sh

test_expect_success setup '
	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag mark &&
	git tag mark1.1 &&
	git tag mark1.2 &&
	git tag mark1.10 &&
	git show-ref --tags -d | sed -e "s/ /	/" >expected.tag &&
	(
		echo "$(git rev-parse HEAD)	HEAD" &&
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

test_expect_success 'ls-remote --sort="version:refname" --tags self' '
	cat >expect <<-EOF &&
	$(git rev-parse mark)	refs/tags/mark
	$(git rev-parse mark1.1)	refs/tags/mark1.1
	$(git rev-parse mark1.2)	refs/tags/mark1.2
	$(git rev-parse mark1.10)	refs/tags/mark1.10
	EOF
	git ls-remote --sort="version:refname" --tags self >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --sort="-version:refname" --tags self' '
	cat >expect <<-EOF &&
	$(git rev-parse mark1.10)	refs/tags/mark1.10
	$(git rev-parse mark1.2)	refs/tags/mark1.2
	$(git rev-parse mark1.1)	refs/tags/mark1.1
	$(git rev-parse mark)	refs/tags/mark
	EOF
	git ls-remote --sort="-version:refname" --tags self >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --sort="-refname" --tags self' '
	cat >expect <<-EOF &&
	$(git rev-parse mark1.2)	refs/tags/mark1.2
	$(git rev-parse mark1.10)	refs/tags/mark1.10
	$(git rev-parse mark1.1)	refs/tags/mark1.1
	$(git rev-parse mark)	refs/tags/mark
	EOF
	git ls-remote --sort="-refname" --tags self >actual &&
	test_cmp expect actual
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
		echo "$(git rev-parse HEAD)	HEAD" &&
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
	if test_have_prereq MINGW
	then
		# Windows does not like asterisks in pathname
		does_not_exist=master
	else
		does_not_exist="refs*master"
	fi &&
	cat >exp <<-EOF &&
	fatal: '\''$does_not_exist'\'' does not appear to be a git repository
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
	test_must_fail git ls-remote "$does_not_exist" >actual 2>&1 &&
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
	test_must_be_empty actual
'

test_expect_success 'Report no-match with --exit-code' '
	test_expect_code 2 git ls-remote --exit-code other.git "refs/nsn/*" >actual &&
	test_must_be_empty actual
'

test_expect_success 'Report match with --exit-code' '
	git ls-remote --exit-code other.git "refs/tags/*" >actual &&
	git ls-remote . tags/mark* >expect &&
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

test_expect_success 'protocol v2 supports hiderefs' '
	test_config uploadpack.hiderefs refs/tags &&
	git -c protocol.version=2 ls-remote . >actual &&
	! grep refs/tags actual
'

test_expect_success 'ls-remote --symref' '
	git fetch origin &&
	cat >expect <<-EOF &&
	ref: refs/heads/master	HEAD
	$(git rev-parse HEAD)	HEAD
	$(git rev-parse refs/heads/master)	refs/heads/master
	$(git rev-parse HEAD)	refs/remotes/origin/HEAD
	$(git rev-parse refs/remotes/origin/master)	refs/remotes/origin/master
	$(git rev-parse refs/tags/mark)	refs/tags/mark
	$(git rev-parse refs/tags/mark1.1)	refs/tags/mark1.1
	$(git rev-parse refs/tags/mark1.10)	refs/tags/mark1.10
	$(git rev-parse refs/tags/mark1.2)	refs/tags/mark1.2
	EOF
	# Protocol v2 supports sending symrefs for refs other than HEAD, so use
	# protocol v0 here.
	GIT_TEST_PROTOCOL_VERSION=0 git ls-remote --symref >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote with filtered symref (refname)' '
	rev=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
	ref: refs/heads/master	HEAD
	$rev	HEAD
	EOF
	# Protocol v2 supports sending symrefs for refs other than HEAD, so use
	# protocol v0 here.
	GIT_TEST_PROTOCOL_VERSION=0 git ls-remote --symref . HEAD >actual &&
	test_cmp expect actual
'

test_expect_failure 'ls-remote with filtered symref (--heads)' '
	git symbolic-ref refs/heads/foo refs/tags/mark &&
	cat >expect <<-EOF &&
	ref: refs/tags/mark	refs/heads/foo
	$rev	refs/heads/foo
	$rev	refs/heads/master
	EOF
	# Protocol v2 supports sending symrefs for refs other than HEAD, so use
	# protocol v0 here.
	GIT_TEST_PROTOCOL_VERSION=0 git ls-remote --symref --heads . >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --symref omits filtered-out matches' '
	cat >expect <<-EOF &&
	$rev	refs/heads/foo
	$rev	refs/heads/master
	EOF
	# Protocol v2 supports sending symrefs for refs other than HEAD, so use
	# protocol v0 here.
	GIT_TEST_PROTOCOL_VERSION=0 git ls-remote --symref --heads . >actual &&
	test_cmp expect actual &&
	GIT_TEST_PROTOCOL_VERSION=0 git ls-remote --symref . "refs/heads/*" >actual &&
	test_cmp expect actual
'

test_lazy_prereq GIT_DAEMON '
	test_bool_env GIT_TEST_GIT_DAEMON true
'

# This test spawns a daemon, so run it only if the user would be OK with
# testing with git-daemon.
test_expect_success PIPE,JGIT,GIT_DAEMON 'indicate no refs in standards-compliant empty remote' '
	test_set_port JGIT_DAEMON_PORT &&
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

test_expect_success 'ls-remote works outside repository' '
	# It is important for this repo to be inside the nongit
	# area, as we want a repo name that does not include
	# slashes (because those inhibit some of our configuration
	# lookups).
	nongit git init --bare dst.git &&
	nongit git ls-remote dst.git
'

test_expect_success 'ls-remote --sort fails gracefully outside repository' '
	# Use a sort key that requires access to the referenced objects.
	nongit test_must_fail git ls-remote --sort=authordate "$TRASH_DIRECTORY" 2>err &&
	test_i18ngrep "^fatal: not a git repository, but the field '\''authordate'\'' requires access to object data" err
'

test_expect_success 'ls-remote patterns work with all protocol versions' '
	git for-each-ref --format="%(objectname)	%(refname)" \
		refs/heads/master refs/remotes/origin/master >expect &&
	git -c protocol.version=1 ls-remote . master >actual.v1 &&
	test_cmp expect actual.v1 &&
	git -c protocol.version=2 ls-remote . master >actual.v2 &&
	test_cmp expect actual.v2
'

test_expect_success 'ls-remote prefixes work with all protocol versions' '
	git for-each-ref --format="%(objectname)	%(refname)" \
		refs/heads/ refs/tags/ >expect &&
	git -c protocol.version=1 ls-remote --heads --tags . >actual.v1 &&
	test_cmp expect actual.v1 &&
	git -c protocol.version=2 ls-remote --heads --tags . >actual.v2 &&
	test_cmp expect actual.v2
'

test_done
