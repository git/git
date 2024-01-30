#!/bin/sh
# Copyright (c) 2010, Jens Lehmann

test_description='Recursive "git fetch" for submodules'

GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh

pwd=$(pwd)

write_expected_sub () {
	NEW_HEAD=$1 &&
	SUPER_HEAD=$2 &&
	cat >"$pwd/expect.err.sub" <<-EOF
	Fetching submodule submodule${SUPER_HEAD:+ at commit $SUPER_HEAD}
	From $pwd/submodule
	   OLD_HEAD..$NEW_HEAD  sub        -> origin/sub
	EOF
}

write_expected_sub2 () {
	NEW_HEAD=$1 &&
	SUPER_HEAD=$2 &&
	cat >"$pwd/expect.err.sub2" <<-EOF
	Fetching submodule submodule2${SUPER_HEAD:+ at commit $SUPER_HEAD}
	From $pwd/submodule2
	   OLD_HEAD..$NEW_HEAD  sub2       -> origin/sub2
	EOF
}

write_expected_deep () {
	NEW_HEAD=$1 &&
	SUB_HEAD=$2 &&
	cat >"$pwd/expect.err.deep" <<-EOF
	Fetching submodule submodule/subdir/deepsubmodule${SUB_HEAD:+ at commit $SUB_HEAD}
	From $pwd/deepsubmodule
	   OLD_HEAD..$NEW_HEAD  deep       -> origin/deep
	EOF
}

write_expected_super () {
	NEW_HEAD=$1 &&
	cat >"$pwd/expect.err.super" <<-EOF
	From $pwd/.
	   OLD_HEAD..$NEW_HEAD  super      -> origin/super
	EOF
}

# For each submodule in the test setup, this creates a commit and writes
# a file that contains the expected err if that new commit were fetched.
# These output files get concatenated in the right order by
# verify_fetch_result().
add_submodule_commits () {
	(
		cd submodule &&
		echo new >> subfile &&
		test_tick &&
		git add subfile &&
		git commit -m new subfile &&
		new_head=$(git rev-parse --short HEAD) &&
		write_expected_sub $new_head
	) &&
	(
		cd deepsubmodule &&
		echo new >> deepsubfile &&
		test_tick &&
		git add deepsubfile &&
		git commit -m new deepsubfile &&
		new_head=$(git rev-parse --short HEAD) &&
		write_expected_deep $new_head
	)
}

# For each superproject in the test setup, update its submodule, add the
# submodule and create a new commit with the submodule change.
#
# This requires add_submodule_commits() to be called first, otherwise
# the submodules will not have changed and cannot be "git add"-ed.
add_superproject_commits () {
	(
		cd submodule &&
		(
			cd subdir/deepsubmodule &&
			git fetch &&
			git checkout -q FETCH_HEAD
		) &&
		git add subdir/deepsubmodule &&
		git commit -m "new deep submodule"
	) &&
	git add submodule &&
	git commit -m "new submodule" &&
	super_head=$(git rev-parse --short HEAD) &&
	sub_head=$(git -C submodule rev-parse --short HEAD) &&
	write_expected_super $super_head &&
	write_expected_sub $sub_head
}

# Verifies that the expected repositories were fetched. This is done by
# concatenating the files expect.err.[super|sub|deep] in the correct
# order and comparing it to the actual stderr.
#
# If a repo should not be fetched in the test, its corresponding
# expect.err file should be rm-ed.
verify_fetch_result () {
	ACTUAL_ERR=$1 &&
	rm -f expect.err.combined &&
	if test -f expect.err.super
	then
		cat expect.err.super >>expect.err.combined
	fi &&
	if test -f expect.err.sub
	then
		cat expect.err.sub >>expect.err.combined
	fi &&
	if test -f expect.err.deep
	then
		cat expect.err.deep >>expect.err.combined
	fi &&
	if test -f expect.err.sub2
	then
		cat expect.err.sub2 >>expect.err.combined
	fi &&
	sed -e 's/[0-9a-f][0-9a-f]*\.\./OLD_HEAD\.\./' "$ACTUAL_ERR" >actual.err.cmp &&
	test_cmp expect.err.combined actual.err.cmp
}

test_expect_success setup '
	git config --global protocol.file.allow always &&
	mkdir deepsubmodule &&
	(
		cd deepsubmodule &&
		git init &&
		echo deepsubcontent > deepsubfile &&
		git add deepsubfile &&
		git commit -m new deepsubfile &&
		git branch -M deep
	) &&
	mkdir submodule &&
	(
		cd submodule &&
		git init &&
		echo subcontent > subfile &&
		git add subfile &&
		git submodule add "$pwd/deepsubmodule" subdir/deepsubmodule &&
		git commit -a -m new &&
		git branch -M sub
	) &&
	git submodule add "$pwd/submodule" submodule &&
	git commit -am initial &&
	git branch -M super &&
	git clone . downstream &&
	(
		cd downstream &&
		git submodule update --init --recursive
	)
'

test_expect_success "fetch --recurse-submodules recurses into submodules" '
	add_submodule_commits &&
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "fetch --recurse-submodules honors --no-write-fetch-head" '
	(
		cd downstream &&
		git submodule foreach --recursive \
		sh -c "cd \"\$(git rev-parse --git-dir)\" && rm -f FETCH_HEAD" &&

		git fetch --recurse-submodules --no-write-fetch-head &&

		git submodule foreach --recursive \
		sh -c "cd \"\$(git rev-parse --git-dir)\" && ! test -f FETCH_HEAD"
	)
'

test_expect_success "submodule.recurse option triggers recursive fetch" '
	add_submodule_commits &&
	(
		cd downstream &&
		git -c submodule.recurse fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "fetch --recurse-submodules -j2 has the same output behaviour" '
	test_when_finished "rm -f trace.out" &&
	add_submodule_commits &&
	(
		cd downstream &&
		GIT_TRACE="$TRASH_DIRECTORY/trace.out" git fetch --recurse-submodules -j2 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err &&
	grep "2 tasks" trace.out
'

test_expect_success "fetch alone only fetches superproject" '
	add_submodule_commits &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "fetch --no-recurse-submodules only fetches superproject" '
	(
		cd downstream &&
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "using fetchRecurseSubmodules=true in .gitmodules recurses into submodules" '
	(
		cd downstream &&
		git config -f .gitmodules submodule.submodule.fetchRecurseSubmodules true &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "--no-recurse-submodules overrides .gitmodules config" '
	add_submodule_commits &&
	(
		cd downstream &&
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "using fetchRecurseSubmodules=false in .git/config overrides setting in .gitmodules" '
	(
		cd downstream &&
		git config submodule.submodule.fetchRecurseSubmodules false &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--recurse-submodules overrides fetchRecurseSubmodules setting from .git/config" '
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err &&
		git config --unset -f .gitmodules submodule.submodule.fetchRecurseSubmodules &&
		git config --unset submodule.submodule.fetchRecurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "--quiet propagates to submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules --quiet >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--quiet propagates to parallel submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules -j 2 --quiet  >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--dry-run propagates to submodules" '
	add_submodule_commits &&
	(
		cd downstream &&
		git fetch --recurse-submodules --dry-run >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "Without --dry-run propagates to submodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "recurseSubmodules=true propagates into submodules" '
	add_submodule_commits &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "--recurse-submodules overrides config in submodule" '
	add_submodule_commits &&
	(
		cd downstream &&
		(
			cd submodule &&
			git config fetch.recurseSubmodules false
		) &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "--no-recurse-submodules overrides config setting" '
	add_submodule_commits &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true &&
		git fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "Recursion doesn't happen when no new commits are fetched in the superproject" '
	(
		cd downstream &&
		(
			cd submodule &&
			git config --unset fetch.recurseSubmodules
		) &&
		git config --unset fetch.recurseSubmodules &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "Recursion stops when no new submodule commits are fetched" '
	git add submodule &&
	git commit -m "new submodule" &&
	new_head=$(git rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.deep &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	verify_fetch_result actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "Recursion doesn't happen when new superproject commits don't change any submodules" '
	add_submodule_commits &&
	echo a > file &&
	git add file &&
	git commit -m "new file" &&
	new_head=$(git rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.sub &&
	rm expect.err.deep &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "Recursion picks up config in submodule" '
	(
		cd downstream &&
		git fetch --recurse-submodules &&
		(
			cd submodule &&
			git config fetch.recurseSubmodules true
		)
	) &&
	add_submodule_commits &&
	git add submodule &&
	git commit -m "new submodule" &&
	new_head=$(git rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err &&
		(
			cd submodule &&
			git config --unset fetch.recurseSubmodules
		)
	) &&
	verify_fetch_result actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "Recursion picks up all submodules when necessary" '
	add_submodule_commits &&
	add_superproject_commits &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	verify_fetch_result actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "'--recurse-submodules=on-demand' doesn't recurse when no new commits are fetched in the superproject (and ignores config)" '
	add_submodule_commits &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules true &&
		git fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err &&
		git config --unset fetch.recurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "'--recurse-submodules=on-demand' recurses as deep as necessary (and ignores config)" '
	add_submodule_commits &&
	add_superproject_commits &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules false &&
		(
			cd submodule &&
			git config -f .gitmodules submodule.subdir/deepsubmodule.fetchRecursive false
		) &&
		git fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err &&
		git config --unset fetch.recurseSubmodules &&
		(
			cd submodule &&
			git config --unset -f .gitmodules submodule.subdir/deepsubmodule.fetchRecursive
		)
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

# These tests verify that we can fetch submodules that aren't in the
# index.
#
# First, test the simple case where the index is empty and we only fetch
# submodules that are not in the index.
test_expect_success 'setup downstream branch without submodules' '
	(
		cd downstream &&
		git checkout --recurse-submodules -b no-submodules &&
		git rm .gitmodules &&
		git rm submodule &&
		git commit -m "no submodules" &&
		git checkout --recurse-submodules super
	)
'

test_expect_success "'--recurse-submodules=on-demand' should fetch submodule commits if the submodule is changed but the index has no submodules" '
	add_submodule_commits &&
	add_superproject_commits &&
	# Fetch the new superproject commit
	(
		cd downstream &&
		git switch --recurse-submodules no-submodules &&
		git fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err
	) &&
	super_head=$(git rev-parse --short HEAD) &&
	sub_head=$(git -C submodule rev-parse --short HEAD) &&
	deep_head=$(git -C submodule/subdir/deepsubmodule rev-parse --short HEAD) &&

	# assert that these are fetched from commits, not the index
	write_expected_sub $sub_head $super_head &&
	write_expected_deep $deep_head $sub_head &&

	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "'--recurse-submodules' should fetch submodule commits if the submodule is changed but the index has no submodules" '
	add_submodule_commits &&
	add_superproject_commits &&
	# Fetch the new superproject commit
	(
		cd downstream &&
		git switch --recurse-submodules no-submodules &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	super_head=$(git rev-parse --short HEAD) &&
	sub_head=$(git -C submodule rev-parse --short HEAD) &&
	deep_head=$(git -C submodule/subdir/deepsubmodule rev-parse --short HEAD) &&

	# assert that these are fetched from commits, not the index
	write_expected_sub $sub_head $super_head &&
	write_expected_deep $deep_head $sub_head &&

	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "'--recurse-submodules' should ignore changed, inactive submodules" '
	add_submodule_commits &&
	add_superproject_commits &&

	# Fetch the new superproject commit
	(
		cd downstream &&
		git switch --recurse-submodules no-submodules &&
		git -c submodule.submodule.active=false fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	super_head=$(git rev-parse --short HEAD) &&
	write_expected_super $super_head &&
	# Neither should be fetched because the submodule is inactive
	rm expect.err.sub &&
	rm expect.err.deep &&
	verify_fetch_result actual.err
'

# Now that we know we can fetch submodules that are not in the index,
# test that we can fetch index and non-index submodules in the same
# operation.
test_expect_success 'setup downstream branch with other submodule' '
	mkdir submodule2 &&
	(
		cd submodule2 &&
		git init &&
		echo sub2content >sub2file &&
		git add sub2file &&
		git commit -a -m new &&
		git branch -M sub2
	) &&
	git checkout -b super-sub2-only &&
	git submodule add "$pwd/submodule2" submodule2 &&
	git commit -m "add sub2" &&
	git checkout super &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin &&
		git checkout super-sub2-only &&
		# Explicitly run "git submodule update" because sub2 is new
		# and has not been cloned.
		git submodule update --init &&
		git checkout --recurse-submodules super
	)
'

test_expect_success "'--recurse-submodules' should fetch submodule commits in changed submodules and the index" '
	test_when_finished "rm expect.err.sub2" &&
	# Create new commit in origin/super
	add_submodule_commits &&
	add_superproject_commits &&

	# Create new commit in origin/super-sub2-only
	git checkout super-sub2-only &&
	(
		cd submodule2 &&
		test_commit --no-tag foo
	) &&
	git add submodule2 &&
	git commit -m "new submodule2" &&

	git checkout super &&
	(
		cd downstream &&
		git fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	sub2_head=$(git -C submodule2 rev-parse --short HEAD) &&
	super_head=$(git rev-parse --short super) &&
	super_sub2_only_head=$(git rev-parse --short super-sub2-only) &&
	write_expected_sub2 $sub2_head $super_sub2_only_head &&

	# write_expected_super cannot handle >1 branch. Since this is a
	# one-off, construct expect.err.super manually.
	cat >"$pwd/expect.err.super" <<-EOF &&
	From $pwd/.
	   OLD_HEAD..$super_head  super           -> origin/super
	   OLD_HEAD..$super_sub2_only_head  super-sub2-only -> origin/super-sub2-only
	EOF
	verify_fetch_result actual.err
'

test_expect_success "'--recurse-submodules=on-demand' stops when no new submodule commits are found in the superproject (and ignores config)" '
	add_submodule_commits &&
	echo a >> file &&
	git add file &&
	git commit -m "new file" &&
	new_head=$(git rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.sub &&
	rm expect.err.deep &&
	(
		cd downstream &&
		git fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "'fetch.recurseSubmodules=on-demand' overrides global config" '
	(
		cd downstream &&
		git fetch --recurse-submodules
	) &&
	add_submodule_commits &&
	git config --global fetch.recurseSubmodules false &&
	git add submodule &&
	git commit -m "new submodule" &&
	new_head=$(git rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.deep &&
	(
		cd downstream &&
		git config fetch.recurseSubmodules on-demand &&
		git fetch >../actual.out 2>../actual.err
	) &&
	git config --global --unset fetch.recurseSubmodules &&
	(
		cd downstream &&
		git config --unset fetch.recurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "'submodule.<sub>.fetchRecurseSubmodules=on-demand' overrides fetch.recurseSubmodules" '
	(
		cd downstream &&
		git fetch --recurse-submodules
	) &&
	add_submodule_commits &&
	git config fetch.recurseSubmodules false &&
	git add submodule &&
	git commit -m "new submodule" &&
	new_head=$(git rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.deep &&
	(
		cd downstream &&
		git config submodule.submodule.fetchRecurseSubmodules on-demand &&
		git fetch >../actual.out 2>../actual.err
	) &&
	git config --unset fetch.recurseSubmodules &&
	(
		cd downstream &&
		git config --unset submodule.submodule.fetchRecurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "don't fetch submodule when newly recorded commits are already present" '
	(
		cd submodule &&
		git checkout -q HEAD^^
	) &&
	git add submodule &&
	git commit -m "submodule rewound" &&
	new_head=$(git rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.sub &&
	# This file does not exist, but rm -f for readability
	rm -f expect.err.deep &&
	(
		cd downstream &&
		git fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err &&
	(
		cd submodule &&
		git checkout -q sub
	)
'

test_expect_success "'fetch.recurseSubmodules=on-demand' works also without .gitmodules entry" '
	(
		cd downstream &&
		git fetch --recurse-submodules
	) &&
	add_submodule_commits &&
	git add submodule &&
	git rm .gitmodules &&
	git commit -m "new submodule without .gitmodules" &&
	new_head=$(git rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.deep &&
	(
		cd downstream &&
		rm .gitmodules &&
		git config fetch.recurseSubmodules on-demand &&
		# fake submodule configuration to avoid skipping submodule handling
		git config -f .gitmodules submodule.fake.path fake &&
		git config -f .gitmodules submodule.fake.url fakeurl &&
		git add .gitmodules &&
		git config --unset submodule.submodule.url &&
		git fetch >../actual.out 2>../actual.err &&
		# cleanup
		git config --unset fetch.recurseSubmodules &&
		git reset --hard
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err &&
	git checkout HEAD^ -- .gitmodules &&
	git add .gitmodules &&
	git commit -m "new submodule restored .gitmodules"
'

test_expect_success 'fetching submodules respects parallel settings' '
	git config fetch.recurseSubmodules true &&
	test_when_finished "rm -f downstream/trace.out" &&
	(
		cd downstream &&
		GIT_TRACE=$(pwd)/trace.out git fetch &&
		grep "1 tasks" trace.out &&
		>trace.out &&

		GIT_TRACE=$(pwd)/trace.out git fetch --jobs 7 &&
		grep "7 tasks" trace.out &&
		>trace.out &&

		git config submodule.fetchJobs 8 &&
		GIT_TRACE=$(pwd)/trace.out git fetch &&
		grep "8 tasks" trace.out &&
		>trace.out &&

		GIT_TRACE=$(pwd)/trace.out git fetch --jobs 9 &&
		grep "9 tasks" trace.out &&
		>trace.out &&

		GIT_TRACE=$(pwd)/trace.out git -c submodule.fetchJobs=0 fetch &&
		grep "preparing to run up to [0-9]* tasks" trace.out &&
		! grep "up to 0 tasks" trace.out &&
		>trace.out
	)
'

test_expect_success 'fetching submodule into a broken repository' '
	# Prepare src and src/sub nested in it
	git init src &&
	(
		cd src &&
		git init sub &&
		git -C sub commit --allow-empty -m "initial in sub" &&
		git submodule add -- ./sub sub &&
		git commit -m "initial in top"
	) &&

	# Clone the old-fashoned way
	git clone src dst &&
	git -C dst clone ../src/sub sub &&

	# Make sure that old-fashoned layout is still supported
	git -C dst status &&

	# "diff" would find no change
	git -C dst diff --exit-code &&

	# Recursive-fetch works fine
	git -C dst fetch --recurse-submodules &&

	# Break the receiving submodule
	test-tool -C dst/sub ref-store main delete-refs REF_NO_DEREF msg HEAD &&

	# NOTE: without the fix the following tests will recurse forever!
	# They should terminate with an error.

	test_must_fail git -C dst status &&
	test_must_fail git -C dst diff &&
	test_must_fail git -C dst fetch --recurse-submodules
'

test_expect_success "fetch new commits when submodule got renamed" '
	git clone . downstream_rename &&
	(
		cd downstream_rename &&
		git submodule update --init --recursive &&
		git checkout -b rename &&
		git mv submodule submodule_renamed &&
		(
			cd submodule_renamed &&
			git checkout -b rename_sub &&
			echo a >a &&
			git add a &&
			git commit -ma &&
			git push origin rename_sub &&
			git rev-parse HEAD >../../expect
		) &&
		git add submodule_renamed &&
		git commit -m "update renamed submodule" &&
		git push origin rename
	) &&
	(
		cd downstream &&
		git fetch --recurse-submodules=on-demand &&
		(
			cd submodule &&
			git rev-parse origin/rename_sub >../../actual
		)
	) &&
	test_cmp expect actual
'

test_expect_success "fetch new submodule commits on-demand outside standard refspec" '
	# add a second submodule and ensure it is around in downstream first
	git clone submodule sub1 &&
	git submodule add ./sub1 &&
	git commit -m "adding a second submodule" &&
	git -C downstream pull &&
	git -C downstream submodule update --init --recursive &&

	git checkout --detach &&

	C=$(git -C submodule commit-tree -m "new change outside refs/heads" HEAD^{tree}) &&
	git -C submodule update-ref refs/changes/1 $C &&
	git update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(git -C sub1 commit-tree -m "new change outside refs/heads" HEAD^{tree}) &&
	git -C sub1 update-ref refs/changes/2 $D &&
	git update-index --cacheinfo 160000 $D sub1 &&

	git commit -m "updated submodules outside of refs/heads" &&
	E=$(git rev-parse HEAD) &&
	git update-ref refs/changes/3 $E &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin refs/changes/3:refs/heads/my_branch &&
		git -C submodule cat-file -t $C &&
		git -C sub1 cat-file -t $D &&
		git checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule commit on-demand in FETCH_HEAD' '
	# depends on the previous test for setup

	C=$(git -C submodule commit-tree -m "another change outside refs/heads" HEAD^{tree}) &&
	git -C submodule update-ref refs/changes/4 $C &&
	git update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(git -C sub1 commit-tree -m "another change outside refs/heads" HEAD^{tree}) &&
	git -C sub1 update-ref refs/changes/5 $D &&
	git update-index --cacheinfo 160000 $D sub1 &&

	git commit -m "updated submodules outside of refs/heads" &&
	E=$(git rev-parse HEAD) &&
	git update-ref refs/changes/6 $E &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin refs/changes/6 &&
		git -C submodule cat-file -t $C &&
		git -C sub1 cat-file -t $D &&
		git checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule commits on-demand without .gitmodules entry' '
	# depends on the previous test for setup

	git config -f .gitmodules --remove-section submodule.sub1 &&
	git add .gitmodules &&
	git commit -m "delete gitmodules file" &&
	git checkout -B super &&
	git -C downstream fetch &&
	git -C downstream checkout origin/super &&

	C=$(git -C submodule commit-tree -m "yet another change outside refs/heads" HEAD^{tree}) &&
	git -C submodule update-ref refs/changes/7 $C &&
	git update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(git -C sub1 commit-tree -m "yet another change outside refs/heads" HEAD^{tree}) &&
	git -C sub1 update-ref refs/changes/8 $D &&
	git update-index --cacheinfo 160000 $D sub1 &&

	git commit -m "updated submodules outside of refs/heads" &&
	E=$(git rev-parse HEAD) &&
	git update-ref refs/changes/9 $E &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin refs/changes/9 &&
		git -C submodule cat-file -t $C &&
		git -C sub1 cat-file -t $D &&
		git checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule commit intermittently referenced by superproject' '
	# depends on the previous test for setup

	D=$(git -C sub1 commit-tree -m "change 10 outside refs/heads" HEAD^{tree}) &&
	E=$(git -C sub1 commit-tree -m "change 11 outside refs/heads" HEAD^{tree}) &&
	F=$(git -C sub1 commit-tree -m "change 12 outside refs/heads" HEAD^{tree}) &&

	git -C sub1 update-ref refs/changes/10 $D &&
	git update-index --cacheinfo 160000 $D sub1 &&
	git commit -m "updated submodules outside of refs/heads" &&

	git -C sub1 update-ref refs/changes/11 $E &&
	git update-index --cacheinfo 160000 $E sub1 &&
	git commit -m "updated submodules outside of refs/heads" &&

	git -C sub1 update-ref refs/changes/12 $F &&
	git update-index --cacheinfo 160000 $F sub1 &&
	git commit -m "updated submodules outside of refs/heads" &&

	G=$(git rev-parse HEAD) &&
	git update-ref refs/changes/13 $G &&
	(
		cd downstream &&
		git fetch --recurse-submodules origin refs/changes/13 &&

		git -C sub1 cat-file -t $D &&
		git -C sub1 cat-file -t $E &&
		git -C sub1 cat-file -t $F
	)
'

add_commit_push () {
	dir="$1" &&
	msg="$2" &&
	shift 2 &&
	git -C "$dir" add "$@" &&
	git -C "$dir" commit -a -m "$msg" &&
	git -C "$dir" push
}

compare_refs_in_dir () {
	fail= &&
	if test "x$1" = 'x!'
	then
		fail='!' &&
		shift
	fi &&
	git -C "$1" rev-parse --verify "$2" >expect &&
	git -C "$3" rev-parse --verify "$4" >actual &&
	eval $fail test_cmp expect actual
}


test_expect_success 'setup nested submodule fetch test' '
	# does not depend on any previous test setups

	for repo in outer middle inner
	do
		git init --bare $repo &&
		git clone $repo ${repo}_content &&
		echo "$repo" >"${repo}_content/file" &&
		add_commit_push ${repo}_content "initial" file ||
		return 1
	done &&

	git clone outer A &&
	git -C A submodule add "$pwd/middle" &&
	git -C A/middle/ submodule add "$pwd/inner" &&
	add_commit_push A/middle/ "adding inner sub" .gitmodules inner &&
	add_commit_push A/ "adding middle sub" .gitmodules middle &&

	git clone outer B &&
	git -C B/ submodule update --init middle &&

	compare_refs_in_dir A HEAD B HEAD &&
	compare_refs_in_dir A/middle HEAD B/middle HEAD &&
	test_path_is_file B/file &&
	test_path_is_file B/middle/file &&
	test_path_is_missing B/middle/inner/file &&

	echo "change on inner repo of A" >"A/middle/inner/file" &&
	add_commit_push A/middle/inner "change on inner" file &&
	add_commit_push A/middle "change on inner" inner &&
	add_commit_push A "change on inner" middle
'

test_expect_success 'fetching a superproject containing an uninitialized sub/sub project' '
	# depends on previous test for setup

	git -C B/ fetch &&
	compare_refs_in_dir A origin/HEAD B origin/HEAD
'

fetch_with_recursion_abort () {
	# In a regression the following git call will run into infinite recursion.
	# To handle that, we connect the sed command to the git call by a pipe
	# so that sed can kill the infinite recursion when detected.
	# The recursion creates git output like:
	# Fetching submodule sub
	# Fetching submodule sub/sub              <-- [1]
	# Fetching submodule sub/sub/sub
	# ...
	# [1] sed will stop reading and cause git to eventually stop and die

	git -C "$1" fetch --recurse-submodules 2>&1 |
		sed "/Fetching submodule $2[^$]/q" >out &&
	! grep "Fetching submodule $2[^$]" out
}

test_expect_success 'setup recursive fetch with uninit submodule' '
	# does not depend on any previous test setups

	test_create_repo super &&
	test_commit -C super initial &&
	test_create_repo sub &&
	test_commit -C sub initial &&
	git -C sub rev-parse HEAD >expect &&

	git -C super submodule add ../sub &&
	git -C super commit -m "add sub" &&

	git clone super superclone &&
	git -C superclone submodule status >out &&
	sed -e "s/^-//" -e "s/ sub.*$//" out >actual &&
	test_cmp expect actual
'

test_expect_success 'recursive fetch with uninit submodule' '
	# depends on previous test for setup

	fetch_with_recursion_abort superclone sub &&
	git -C superclone submodule status >out &&
	sed -e "s/^-//" -e "s/ sub$//" out >actual &&
	test_cmp expect actual
'

test_expect_success 'recursive fetch after deinit a submodule' '
	# depends on previous test for setup

	git -C superclone submodule update --init sub &&
	git -C superclone submodule deinit -f sub &&

	fetch_with_recursion_abort superclone sub &&
	git -C superclone submodule status >out &&
	sed -e "s/^-//" -e "s/ sub$//" out >actual &&
	test_cmp expect actual
'

test_expect_success 'setup repo with upstreams that share a submodule name' '
	mkdir same-name-1 &&
	(
		cd same-name-1 &&
		git init -b main &&
		test_commit --no-tag a
	) &&
	git clone same-name-1 same-name-2 &&
	# same-name-1 and same-name-2 both add a submodule with the
	# name "submodule"
	(
		cd same-name-1 &&
		mkdir submodule &&
		git -C submodule init -b main &&
		test_commit -C submodule --no-tag a1 &&
		git submodule add "$pwd/same-name-1/submodule" &&
		git add submodule &&
		git commit -m "super-a1"
	) &&
	(
		cd same-name-2 &&
		mkdir submodule &&
		git -C submodule init -b main &&
		test_commit -C submodule --no-tag a2 &&
		git submodule add "$pwd/same-name-2/submodule" &&
		git add submodule &&
		git commit -m "super-a2"
	) &&
	git clone same-name-1 -o same-name-1 same-name-downstream &&
	(
		cd same-name-downstream &&
		git remote add same-name-2 ../same-name-2 &&
		git fetch --all &&
		# init downstream with same-name-1
		git submodule update --init
	)
'

test_expect_success 'fetch --recurse-submodules updates name-conflicted, populated submodule' '
	test_when_finished "git -C same-name-downstream checkout main" &&
	(
		cd same-name-1 &&
		test_commit -C submodule --no-tag b1 &&
		git add submodule &&
		git commit -m "super-b1"
	) &&
	(
		cd same-name-2 &&
		test_commit -C submodule --no-tag b2 &&
		git add submodule &&
		git commit -m "super-b2"
	) &&
	(
		cd same-name-downstream &&
		# even though the .gitmodules is correct, we cannot
		# fetch from same-name-2
		git checkout same-name-2/main &&
		git fetch --recurse-submodules same-name-1 &&
		test_must_fail git fetch --recurse-submodules same-name-2
	) &&
	super_head1=$(git -C same-name-1 rev-parse HEAD) &&
	git -C same-name-downstream cat-file -e $super_head1 &&

	super_head2=$(git -C same-name-2 rev-parse HEAD) &&
	git -C same-name-downstream cat-file -e $super_head2 &&

	sub_head1=$(git -C same-name-1/submodule rev-parse HEAD) &&
	git -C same-name-downstream/submodule cat-file -e $sub_head1 &&

	sub_head2=$(git -C same-name-2/submodule rev-parse HEAD) &&
	test_must_fail git -C same-name-downstream/submodule cat-file -e $sub_head2
'

test_expect_success 'fetch --recurse-submodules updates name-conflicted, unpopulated submodule' '
	(
		cd same-name-1 &&
		test_commit -C submodule --no-tag c1 &&
		git add submodule &&
		git commit -m "super-c1"
	) &&
	(
		cd same-name-2 &&
		test_commit -C submodule --no-tag c2 &&
		git add submodule &&
		git commit -m "super-c2"
	) &&
	(
		cd same-name-downstream &&
		git checkout main &&
		git rm .gitmodules &&
		git rm submodule &&
		git commit -m "no submodules" &&
		git fetch --recurse-submodules same-name-1
	) &&
	head1=$(git -C same-name-1/submodule rev-parse HEAD) &&
	head2=$(git -C same-name-2/submodule rev-parse HEAD) &&
	(
		cd same-name-downstream/.git/modules/submodule &&
		# The submodule has core.worktree pointing to the "git
		# rm"-ed directory, overwrite the invalid value. See
		# comment in get_fetch_task_from_changed() for more
		# information.
		git --work-tree=. cat-file -e $head1 &&
		test_must_fail git --work-tree=. cat-file -e $head2
	)
'

test_expect_success 'fetch --all with --recurse-submodules' '
	test_when_finished "rm -fr src_clone" &&
	git clone --recurse-submodules src src_clone &&
	(
		cd src_clone &&
		git config submodule.recurse true &&
		git config fetch.parallel 0 &&
		git fetch --all 2>../fetch-log
	) &&
	grep "^Fetching submodule sub$" fetch-log >fetch-subs &&
	test_line_count = 1 fetch-subs
'

test_expect_success 'fetch --all with --recurse-submodules with multiple' '
	test_when_finished "rm -fr src_clone" &&
	git clone --recurse-submodules src src_clone &&
	(
		cd src_clone &&
		git remote add secondary ../src &&
		git config submodule.recurse true &&
		git config fetch.parallel 0 &&
		git fetch --all 2>../fetch-log
	) &&
	grep "Fetching submodule sub" fetch-log >fetch-subs &&
	test_line_count = 2 fetch-subs
'

test_expect_success "fetch --all with --no-recurse-submodules only fetches superproject" '
	test_when_finished "rm -rf src_clone" &&

	git clone --recurse-submodules src src_clone &&
	(
		cd src_clone &&
		git remote add secondary ../src &&
		git config submodule.recurse true &&
		git fetch --all --no-recurse-submodules 2>../fetch-log
	) &&
	! grep "Fetching submodule" fetch-log
'

test_done
