#!/bin/sh

test_description='git ls-remote'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

generate_references () {
	for ref
	do
		oid=$(git rev-parse "$ref") &&
		printf '%s\t%s\n' "$oid" "$ref" || return 1
	done
}

test_expect_success 'set up fake upload-pack' '
	# This can be used to simulate an upload-pack that just shows the
	# contents of the "input" file (prepared with the test-tool pkt-line
	# helper), and does not do any negotiation (since ls-remote does not
	# need it).
	write_script cat-input <<-\EOF
	# send our initial advertisement/response
	cat input
	# soak up the flush packet from the client
	cat
	EOF
'

test_expect_success 'dies when no remote found' '
	test_must_fail git ls-remote
'

test_expect_success setup '
	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&
	git tag mark &&
	git tag mark1.1 &&
	git tag mark1.2 &&
	git tag mark1.10 &&
	git show-ref --tags -d >expected.tag.raw &&
	sed -e "s/ /	/" expected.tag.raw >expected.tag &&
	generate_references HEAD >expected.all &&
	git show-ref -d	>refs &&
	sed -e "s/ /	/" refs >>expected.all &&

	grep refs/heads/ expected.all >expected.branches &&
	git remote add self "$(pwd)/.git" &&
	git remote add self2 "."
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

test_expect_success 'ls-remote --branches self' '
	git ls-remote --branches self >actual &&
	test_cmp expected.branches actual &&
	git ls-remote -b self >actual &&
	test_cmp expected.branches actual
'

test_expect_success 'ls-remote -h is deprecated w/o warning' '
	git ls-remote -h self >actual 2>warning &&
	test_cmp expected.branches actual &&
	test_grep ! deprecated warning
'

test_expect_success 'ls-remote --heads is deprecated and hidden w/o warning' '
	test_expect_code 129 git ls-remote -h >short-help &&
	test_grep ! -e --head short-help &&
	git ls-remote --heads self >actual 2>warning &&
	test_cmp expected.branches actual &&
	test_grep ! deprecated warning
'

test_expect_success 'ls-remote --sort="version:refname" --tags self' '
	generate_references \
		refs/tags/mark \
		refs/tags/mark1.1 \
		refs/tags/mark1.2 \
		refs/tags/mark1.10 >expect &&
	git ls-remote --sort="version:refname" --tags self >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --sort="-version:refname" --tags self' '
	generate_references \
		refs/tags/mark1.10 \
		refs/tags/mark1.2 \
		refs/tags/mark1.1 \
		refs/tags/mark >expect &&
	git ls-remote --sort="-version:refname" --tags self >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote --sort="-refname" --tags self' '
	generate_references \
		refs/tags/mark1.2 \
		refs/tags/mark1.10 \
		refs/tags/mark1.1 \
		refs/tags/mark >expect &&
	git ls-remote --sort="-refname" --tags self >actual &&
	test_cmp expect actual
'

test_expect_success 'dies when no remote specified, multiple remotes found, and no default specified' '
	test_must_fail git ls-remote
'

test_expect_success 'succeeds when no remote specified but only one found' '
	test_when_finished git remote add self2 "." &&
	git remote remove self2 &&
	git ls-remote
'

test_expect_success 'use "origin" when no remote specified and multiple found' '
	URL="$(pwd)/.git" &&
	echo "From $URL" >exp_err &&

	git remote add origin "$URL" &&
	git ls-remote 2>actual_err >actual &&

	test_cmp exp_err actual_err &&
	test_cmp expected.all actual
'

test_expect_success 'suppress "From <url>" with -q' '
	git ls-remote -q 2>actual_err &&
	! test_cmp exp_err actual_err
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
	git config branch.main.remote other &&

	git ls-remote 2>actual_err >actual &&
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
	fatal: '\''$does_not_exist'\'' does not appear to be a git repository
	fatal: Could not read from remote repository.

	Please make sure you have the correct access rights
	and the repository exists.
	EOF
	#
	# Do not expect "git ls-remote <pattern>" to work; ls-remote needs
	# <remote> if you want to feed <pattern>, just like you cannot say
	# fetch <branch>.
	# We could just as easily have used "main"; the "*" emphasizes its
	# role as a pattern.
	test_must_fail git ls-remote "$does_not_exist" >actual 2>&1 &&
	test_cmp exp actual
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
		git ls-remote . >expect.raw &&
		sed -e "/	refs\/tags\//d" expect.raw >expect &&
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
	echo "ref: refs/heads/main	HEAD" >expect.v2 &&
	generate_references \
		HEAD \
		refs/heads/main >>expect.v2 &&
	echo "ref: refs/remotes/origin/main	refs/remotes/origin/HEAD" >>expect.v2 &&
	oid=$(git rev-parse HEAD) &&
	echo "$oid	refs/remotes/origin/HEAD" >>expect.v2 &&
	generate_references \
		refs/remotes/origin/main \
		refs/tags/mark \
		refs/tags/mark1.1 \
		refs/tags/mark1.10 \
		refs/tags/mark1.2 >>expect.v2 &&
	# v0 does not show non-HEAD symrefs
	grep -v "ref: refs/remotes" <expect.v2 >expect.v0 &&
	git -c protocol.version=0 ls-remote --symref >actual.v0 &&
	test_cmp expect.v0 actual.v0 &&
	git -c protocol.version=2 ls-remote --symref >actual.v2 &&
	test_cmp expect.v2 actual.v2
'

test_expect_success 'ls-remote with filtered symref (refname)' '
	rev=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
	ref: refs/heads/main	HEAD
	$rev	HEAD
	ref: refs/remotes/origin/main	refs/remotes/origin/HEAD
	$rev	refs/remotes/origin/HEAD
	EOF
	git ls-remote --symref . HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'ls-remote with filtered symref (--branches)' '
	git symbolic-ref refs/heads/foo refs/tags/mark &&
	cat >expect.v2 <<-EOF &&
	ref: refs/tags/mark	refs/heads/foo
	$rev	refs/heads/foo
	$rev	refs/heads/main
	EOF
	grep -v "^ref: refs/tags/" <expect.v2 >expect.v0 &&
	git -c protocol.version=0 ls-remote --symref --branches . >actual.v0 &&
	test_cmp expect.v0 actual.v0 &&
	git -c protocol.version=2 ls-remote --symref --branches . >actual.v2 &&
	test_cmp expect.v2 actual.v2
'

test_expect_success 'indicate no refs in v0 standards-compliant empty remote' '
	# Git does not produce an output like this, but it does match the
	# standard and is produced by other implementations like JGit. So
	# hard-code the case we care about.
	#
	# The actual capabilities do not matter; there are none that would
	# change how ls-remote behaves.
	oid=0000000000000000000000000000000000000000 &&
	test-tool pkt-line pack >input.q <<-EOF &&
	$oid capabilities^{}Qcaps-go-here
	0000
	EOF
	q_to_nul <input.q >input &&

	# --exit-code asks the command to exit with 2 when no
	# matching refs are found.
	test_expect_code 2 git ls-remote --exit-code --upload-pack=./cat-input .
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
	test_grep "^fatal: not a git repository, but the field '\''authordate'\'' requires access to object data" err
'

test_expect_success 'ls-remote patterns work with all protocol versions' '
	git for-each-ref --format="%(objectname)	%(refname)" \
		refs/heads/main refs/remotes/origin/main >expect &&
	git -c protocol.version=0 ls-remote . main >actual.v0 &&
	test_cmp expect actual.v0 &&
	git -c protocol.version=2 ls-remote . main >actual.v2 &&
	test_cmp expect actual.v2
'

test_expect_success 'ls-remote prefixes work with all protocol versions' '
	git for-each-ref --format="%(objectname)	%(refname)" \
		refs/heads/ refs/tags/ >expect &&
	git -c protocol.version=0 ls-remote --branches --tags . >actual.v0 &&
	test_cmp expect actual.v0 &&
	git -c protocol.version=2 ls-remote --branches --tags . >actual.v2 &&
	test_cmp expect actual.v2
'

test_expect_success 'v0 clients can handle multiple symrefs' '
	# Modern versions of Git will not return multiple symref capabilities
	# for v0, so we have to hard-code the response. Note that we will
	# always use both v0 and object-format=sha1 here, as the hard-coded
	# response reflects a server that only supports those.
	oid=1234567890123456789012345678901234567890 &&
	symrefs="symref=refs/remotes/origin/HEAD:refs/remotes/origin/main" &&
	symrefs="$symrefs symref=HEAD:refs/heads/main" &&

	# Likewise we want to make sure our parser is not fooled by the string
	# "symref" appearing as part of an earlier cap. But there is no way to
	# do that via upload-pack, as arbitrary strings can appear only in a
	# "symref" value itself (where we skip past the values as a whole)
	# and "agent" (which always appears after "symref", so putting our
	# parser in a confused state is less interesting).
	caps="some other caps including a-fake-symref-cap" &&

	test-tool pkt-line pack >input.q <<-EOF &&
	$oid HEADQ$caps $symrefs
	$oid refs/heads/main
	$oid refs/remotes/origin/HEAD
	$oid refs/remotes/origin/main
	0000
	EOF
	q_to_nul <input.q >input &&

	cat >expect <<-EOF &&
	ref: refs/heads/main	HEAD
	$oid	HEAD
	$oid	refs/heads/main
	ref: refs/remotes/origin/main	refs/remotes/origin/HEAD
	$oid	refs/remotes/origin/HEAD
	$oid	refs/remotes/origin/main
	EOF

	git ls-remote --symref --upload-pack=./cat-input . >actual &&
	test_cmp expect actual
'

test_expect_success 'helper with refspec capability fails gracefully' '
	mkdir test-bin &&
	write_script test-bin/git-remote-foo <<-EOF &&
	read capabilities
	echo import
	echo refspec ${SQ}*:*${SQ}
	EOF
	(
		PATH="$PWD/test-bin:$PATH" &&
		export PATH &&
		test_must_fail nongit git ls-remote foo::bar
	)
'

test_done
