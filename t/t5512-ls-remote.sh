#!/bin/sh

test_description='but ls-remote'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

generate_references () {
	for ref
	do
		oid=$(but rev-parse "$ref") &&
		printf '%s\t%s\n' "$oid" "$ref" || return 1
	done
}

test_expect_success setup '
	>file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&
	but tag mark &&
	but tag mark1.1 &&
	but tag mark1.2 &&
	but tag mark1.10 &&
	but show-ref --tags -d >expected.tag.raw &&
	sed -e "s/ /	/" expected.tag.raw >expected.tag &&
	generate_references HEAD >expected.all &&
	but show-ref -d	>refs &&
	sed -e "s/ /	/" refs >>expected.all &&

	but remote add self "$(pwd)/.but"
'

test_expect_success 'ls-remote --tags .but' '
	but ls-remote --tags .but >actual &&
	test_cmp expected.tag actual
'

test_expect_success 'ls-remote .but' '
	but ls-remote .but >actual &&
	test_cmp expected.all actual
'

test_expect_success 'ls-remote --tags self' '
	but ls-remote --tags self >actual &&
	test_cmp expected.tag actual
'

test_expect_success 'ls-remote self' '
	but ls-remote self >actual &&
	test_cmp expected.all actual
'

test_expect_success 'ls-remote --sort="version:refname" --tags self' '
	generate_references \
		refs/tags/mark \
		refs/tags/mark1.1 \
		refs/tags/mark1.2 \
		refs/tags/mark1.10 >expect &&
	but ls-remote --sort="version:refname" --tags self >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --sort="-version:refname" --tags self' '
	generate_references \
		refs/tags/mark1.10 \
		refs/tags/mark1.2 \
		refs/tags/mark1.1 \
		refs/tags/mark >expect &&
	but ls-remote --sort="-version:refname" --tags self >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --sort="-refname" --tags self' '
	generate_references \
		refs/tags/mark1.2 \
		refs/tags/mark1.10 \
		refs/tags/mark1.1 \
		refs/tags/mark >expect &&
	but ls-remote --sort="-refname" --tags self >actual &&
	test_cmp expect actual
'

test_expect_success 'dies when no remote specified and no default remotes found' '
	test_must_fail but ls-remote
'

test_expect_success 'use "origin" when no remote specified' '
	URL="$(pwd)/.but" &&
	echo "From $URL" >exp_err &&

	but remote add origin "$URL" &&
	but ls-remote 2>actual_err >actual &&

	test_cmp exp_err actual_err &&
	test_cmp expected.all actual
'

test_expect_success 'suppress "From <url>" with -q' '
	but ls-remote -q 2>actual_err &&
	! test_cmp exp_err actual_err
'

test_expect_success 'use branch.<name>.remote if possible' '
	#
	# Test that we are indeed using branch.<name>.remote, not "origin", even
	# though the "origin" remote has been set.
	#

	# setup a new remote to differentiate from "origin"
	but clone . other.but &&
	(
		cd other.but &&
		echo "$(but rev-parse HEAD)	HEAD" &&
		but show-ref	| sed -e "s/ /	/"
	) >exp &&

	URL="other.but" &&
	echo "From $URL" >exp_err &&

	but remote add other $URL &&
	but config branch.main.remote other &&

	but ls-remote 2>actual_err >actual &&
	test_cmp exp_err actual_err &&
	test_cmp exp actual
'

test_expect_success 'confuses pattern as remote when no remote specified' '
	if test_have_prereq MINGW
	then
		# Windows does not like asterisks in pathname
		does_not_exist=main
	else
		does_not_exist="refs*main"
	fi &&
	cat >exp <<-EOF &&
	fatal: '\''$does_not_exist'\'' does not appear to be a but repository
	fatal: Could not read from remote repository.

	Please make sure you have the correct access rights
	and the repository exists.
	EOF
	#
	# Do not expect "but ls-remote <pattern>" to work; ls-remote needs
	# <remote> if you want to feed <pattern>, just like you cannot say
	# fetch <branch>.
	# We could just as easily have used "main"; the "*" emphasizes its
	# role as a pattern.
	test_must_fail but ls-remote "$does_not_exist" >actual 2>&1 &&
	test_cmp exp actual
'

test_expect_success 'die with non-2 for wrong repository even with --exit-code' '
	{
		but ls-remote --exit-code ./no-such-repository
		status=$?
	} &&
	test $status != 2 && test $status != 0
'

test_expect_success 'Report success even when nothing matches' '
	but ls-remote other.but "refs/nsn/*" >actual &&
	test_must_be_empty actual
'

test_expect_success 'Report no-match with --exit-code' '
	test_expect_code 2 but ls-remote --exit-code other.but "refs/nsn/*" >actual &&
	test_must_be_empty actual
'

test_expect_success 'Report match with --exit-code' '
	but ls-remote --exit-code other.but "refs/tags/*" >actual &&
	but ls-remote . tags/mark* >expect &&
	test_cmp expect actual
'

test_expect_success 'set up some extra tags for ref hiding' '
	but tag magic/one &&
	but tag magic/two
'

for configsection in transfer uploadpack
do
	test_expect_success "Hide some refs with $configsection.hiderefs" '
		test_config $configsection.hiderefs refs/tags &&
		but ls-remote . >actual &&
		test_unconfig $configsection.hiderefs &&
		but ls-remote . >expect.raw &&
		sed -e "/	refs\/tags\//d" expect.raw >expect &&
		test_cmp expect actual
	'

	test_expect_success "Override hiding of $configsection.hiderefs" '
		test_when_finished "test_unconfig $configsection.hiderefs" &&
		but config --add $configsection.hiderefs refs/tags &&
		but config --add $configsection.hiderefs "!refs/tags/magic" &&
		but config --add $configsection.hiderefs refs/tags/magic/one &&
		but ls-remote . >actual &&
		grep refs/tags/magic/two actual &&
		! grep refs/tags/magic/one actual
	'

done

test_expect_success 'overrides work between mixed transfer/upload-pack hideRefs' '
	test_config uploadpack.hiderefs refs/tags &&
	test_config transfer.hiderefs "!refs/tags/magic" &&
	but ls-remote . >actual &&
	grep refs/tags/magic actual
'

test_expect_success 'protocol v2 supports hiderefs' '
	test_config uploadpack.hiderefs refs/tags &&
	but -c protocol.version=2 ls-remote . >actual &&
	! grep refs/tags actual
'

test_expect_success 'ls-remote --symref' '
	but fetch origin &&
	echo "ref: refs/heads/main	HEAD" >expect &&
	generate_references \
		HEAD \
		refs/heads/main >>expect &&
	oid=$(but rev-parse HEAD) &&
	echo "$oid	refs/remotes/origin/HEAD" >>expect &&
	generate_references \
		refs/remotes/origin/main \
		refs/tags/mark \
		refs/tags/mark1.1 \
		refs/tags/mark1.10 \
		refs/tags/mark1.2 >>expect &&
	# Protocol v2 supports sending symrefs for refs other than HEAD, so use
	# protocol v0 here.
	BUT_TEST_PROTOCOL_VERSION=0 but ls-remote --symref >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote with filtered symref (refname)' '
	rev=$(but rev-parse HEAD) &&
	cat >expect <<-EOF &&
	ref: refs/heads/main	HEAD
	$rev	HEAD
	EOF
	# Protocol v2 supports sending symrefs for refs other than HEAD, so use
	# protocol v0 here.
	BUT_TEST_PROTOCOL_VERSION=0 but ls-remote --symref . HEAD >actual &&
	test_cmp expect actual
'

test_expect_failure 'ls-remote with filtered symref (--heads)' '
	but symbolic-ref refs/heads/foo refs/tags/mark &&
	cat >expect <<-EOF &&
	ref: refs/tags/mark	refs/heads/foo
	$rev	refs/heads/foo
	$rev	refs/heads/main
	EOF
	# Protocol v2 supports sending symrefs for refs other than HEAD, so use
	# protocol v0 here.
	BUT_TEST_PROTOCOL_VERSION=0 but ls-remote --symref --heads . >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --symref omits filtered-out matches' '
	cat >expect <<-EOF &&
	$rev	refs/heads/foo
	$rev	refs/heads/main
	EOF
	# Protocol v2 supports sending symrefs for refs other than HEAD, so use
	# protocol v0 here.
	BUT_TEST_PROTOCOL_VERSION=0 but ls-remote --symref --heads . >actual &&
	test_cmp expect actual &&
	BUT_TEST_PROTOCOL_VERSION=0 but ls-remote --symref . "refs/heads/*" >actual &&
	test_cmp expect actual
'

test_lazy_prereq BUT_DAEMON '
	test_bool_env BUT_TEST_BUT_DAEMON true
'

# This test spawns a daemon, so run it only if the user would be OK with
# testing with but-daemon.
test_expect_success PIPE,JBUT,BUT_DAEMON 'indicate no refs in standards-compliant empty remote' '
	test_set_port JBUT_DAEMON_PORT &&
	JBUT_DAEMON_PID= &&
	but init --bare empty.but &&
	>empty.but/but-daemon-export-ok &&
	mkfifo jbut_daemon_output &&
	{
		jbut daemon --port="$JBUT_DAEMON_PORT" . >jbut_daemon_output &
		JBUT_DAEMON_PID=$!
	} &&
	test_when_finished kill "$JBUT_DAEMON_PID" &&
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
	} <jbut_daemon_output &&
	# --exit-code asks the command to exit with 2 when no
	# matching refs are found.
	test_expect_code 2 but ls-remote --exit-code but://localhost:$JBUT_DAEMON_PORT/empty.but
'

test_expect_success 'ls-remote works outside repository' '
	# It is important for this repo to be inside the nonbut
	# area, as we want a repo name that does not include
	# slashes (because those inhibit some of our configuration
	# lookups).
	nonbut but init --bare dst.but &&
	nonbut but ls-remote dst.but
'

test_expect_success 'ls-remote --sort fails gracefully outside repository' '
	# Use a sort key that requires access to the referenced objects.
	nonbut test_must_fail but ls-remote --sort=authordate "$TRASH_DIRECTORY" 2>err &&
	test_i18ngrep "^fatal: not a but repository, but the field '\''authordate'\'' requires access to object data" err
'

test_expect_success 'ls-remote patterns work with all protocol versions' '
	but for-each-ref --format="%(objectname)	%(refname)" \
		refs/heads/main refs/remotes/origin/main >expect &&
	but -c protocol.version=1 ls-remote . main >actual.v1 &&
	test_cmp expect actual.v1 &&
	but -c protocol.version=2 ls-remote . main >actual.v2 &&
	test_cmp expect actual.v2
'

test_expect_success 'ls-remote prefixes work with all protocol versions' '
	but for-each-ref --format="%(objectname)	%(refname)" \
		refs/heads/ refs/tags/ >expect &&
	but -c protocol.version=1 ls-remote --heads --tags . >actual.v1 &&
	test_cmp expect actual.v1 &&
	but -c protocol.version=2 ls-remote --heads --tags . >actual.v2 &&
	test_cmp expect actual.v2
'

test_done
