#!/bin/sh
# Copyright (c) 2010, Jens Lehmann

test_description='Recursive "but fetch" for submodules'

GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh

pwd=$(pwd)

write_expected_sub () {
	NEW_HEAD=$1 &&
	SUPER_HEAD=$2 &&
	cat >"$pwd/expect.err.sub" <<-EOF
	Fetching submodule submodule${SUPER_HEAD:+ at cummit $SUPER_HEAD}
	From $pwd/submodule
	   OLD_HEAD..$NEW_HEAD  sub        -> origin/sub
	EOF
}

write_expected_sub2 () {
	NEW_HEAD=$1 &&
	SUPER_HEAD=$2 &&
	cat >"$pwd/expect.err.sub2" <<-EOF
	Fetching submodule submodule2${SUPER_HEAD:+ at cummit $SUPER_HEAD}
	From $pwd/submodule2
	   OLD_HEAD..$NEW_HEAD  sub2       -> origin/sub2
	EOF
}

write_expected_deep () {
	NEW_HEAD=$1 &&
	SUB_HEAD=$2 &&
	cat >"$pwd/expect.err.deep" <<-EOF
	Fetching submodule submodule/subdir/deepsubmodule${SUB_HEAD:+ at cummit $SUB_HEAD}
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

# For each submodule in the test setup, this creates a cummit and writes
# a file that contains the expected err if that new cummit were fetched.
# These output files get concatenated in the right order by
# verify_fetch_result().
add_submodule_cummits () {
	(
		cd submodule &&
		echo new >> subfile &&
		test_tick &&
		but add subfile &&
		but cummit -m new subfile &&
		new_head=$(but rev-parse --short HEAD) &&
		write_expected_sub $new_head
	) &&
	(
		cd deepsubmodule &&
		echo new >> deepsubfile &&
		test_tick &&
		but add deepsubfile &&
		but cummit -m new deepsubfile &&
		new_head=$(but rev-parse --short HEAD) &&
		write_expected_deep $new_head
	)
}

# For each superproject in the test setup, update its submodule, add the
# submodule and create a new cummit with the submodule change.
#
# This requires add_submodule_cummits() to be called first, otherwise
# the submodules will not have changed and cannot be "but add"-ed.
add_superproject_cummits () {
	(
		cd submodule &&
		(
			cd subdir/deepsubmodule &&
			but fetch &&
			but checkout -q FETCH_HEAD
		) &&
		but add subdir/deepsubmodule &&
		but cummit -m "new deep submodule"
	) &&
	but add submodule &&
	but cummit -m "new submodule" &&
	super_head=$(but rev-parse --short HEAD) &&
	sub_head=$(but -C submodule rev-parse --short HEAD) &&
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
	mkdir deepsubmodule &&
	(
		cd deepsubmodule &&
		but init &&
		echo deepsubcontent > deepsubfile &&
		but add deepsubfile &&
		but cummit -m new deepsubfile &&
		but branch -M deep
	) &&
	mkdir submodule &&
	(
		cd submodule &&
		but init &&
		echo subcontent > subfile &&
		but add subfile &&
		but submodule add "$pwd/deepsubmodule" subdir/deepsubmodule &&
		but cummit -a -m new &&
		but branch -M sub
	) &&
	but submodule add "$pwd/submodule" submodule &&
	but cummit -am initial &&
	but branch -M super &&
	but clone . downstream &&
	(
		cd downstream &&
		but submodule update --init --recursive
	)
'

test_expect_success "fetch --recurse-submodules recurses into submodules" '
	add_submodule_cummits &&
	(
		cd downstream &&
		but fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "submodule.recurse option triggers recursive fetch" '
	add_submodule_cummits &&
	(
		cd downstream &&
		but -c submodule.recurse fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "fetch --recurse-submodules -j2 has the same output behaviour" '
	add_submodule_cummits &&
	(
		cd downstream &&
		GIT_TRACE="$TRASH_DIRECTORY/trace.out" but fetch --recurse-submodules -j2 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err &&
	grep "2 tasks" trace.out
'

test_expect_success "fetch alone only fetches superproject" '
	add_submodule_cummits &&
	(
		cd downstream &&
		but fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "fetch --no-recurse-submodules only fetches superproject" '
	(
		cd downstream &&
		but fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "using fetchRecurseSubmodules=true in .butmodules recurses into submodules" '
	(
		cd downstream &&
		but config -f .butmodules submodule.submodule.fetchRecurseSubmodules true &&
		but fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "--no-recurse-submodules overrides .butmodules config" '
	add_submodule_cummits &&
	(
		cd downstream &&
		but fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "using fetchRecurseSubmodules=false in .but/config overrides setting in .butmodules" '
	(
		cd downstream &&
		but config submodule.submodule.fetchRecurseSubmodules false &&
		but fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--recurse-submodules overrides fetchRecurseSubmodules setting from .but/config" '
	(
		cd downstream &&
		but fetch --recurse-submodules >../actual.out 2>../actual.err &&
		but config --unset -f .butmodules submodule.submodule.fetchRecurseSubmodules &&
		but config --unset submodule.submodule.fetchRecurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "--quiet propagates to submodules" '
	(
		cd downstream &&
		but fetch --recurse-submodules --quiet >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--quiet propagates to parallel submodules" '
	(
		cd downstream &&
		but fetch --recurse-submodules -j 2 --quiet  >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "--dry-run propagates to submodules" '
	add_submodule_cummits &&
	(
		cd downstream &&
		but fetch --recurse-submodules --dry-run >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "Without --dry-run propagates to submodules" '
	(
		cd downstream &&
		but fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "recurseSubmodules=true propagates into submodules" '
	add_submodule_cummits &&
	(
		cd downstream &&
		but config fetch.recurseSubmodules true &&
		but fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "--recurse-submodules overrides config in submodule" '
	add_submodule_cummits &&
	(
		cd downstream &&
		(
			cd submodule &&
			but config fetch.recurseSubmodules false
		) &&
		but fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "--no-recurse-submodules overrides config setting" '
	add_submodule_cummits &&
	(
		cd downstream &&
		but config fetch.recurseSubmodules true &&
		but fetch --no-recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "Recursion doesn't happen when no new cummits are fetched in the superproject" '
	(
		cd downstream &&
		(
			cd submodule &&
			but config --unset fetch.recurseSubmodules
		) &&
		but config --unset fetch.recurseSubmodules &&
		but fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "Recursion stops when no new submodule cummits are fetched" '
	but add submodule &&
	but cummit -m "new submodule" &&
	new_head=$(but rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.deep &&
	(
		cd downstream &&
		but fetch >../actual.out 2>../actual.err
	) &&
	verify_fetch_result actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "Recursion doesn't happen when new superproject cummits don't change any submodules" '
	add_submodule_cummits &&
	echo a > file &&
	but add file &&
	but cummit -m "new file" &&
	new_head=$(but rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.sub &&
	rm expect.err.deep &&
	(
		cd downstream &&
		but fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "Recursion picks up config in submodule" '
	(
		cd downstream &&
		but fetch --recurse-submodules &&
		(
			cd submodule &&
			but config fetch.recurseSubmodules true
		)
	) &&
	add_submodule_cummits &&
	but add submodule &&
	but cummit -m "new submodule" &&
	new_head=$(but rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	(
		cd downstream &&
		but fetch >../actual.out 2>../actual.err &&
		(
			cd submodule &&
			but config --unset fetch.recurseSubmodules
		)
	) &&
	verify_fetch_result actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "Recursion picks up all submodules when necessary" '
	add_submodule_cummits &&
	add_superproject_cummits &&
	(
		cd downstream &&
		but fetch >../actual.out 2>../actual.err
	) &&
	verify_fetch_result actual.err &&
	test_must_be_empty actual.out
'

test_expect_success "'--recurse-submodules=on-demand' doesn't recurse when no new cummits are fetched in the superproject (and ignores config)" '
	add_submodule_cummits &&
	(
		cd downstream &&
		but config fetch.recurseSubmodules true &&
		but fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err &&
		but config --unset fetch.recurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	test_must_be_empty actual.err
'

test_expect_success "'--recurse-submodules=on-demand' recurses as deep as necessary (and ignores config)" '
	add_submodule_cummits &&
	add_superproject_cummits &&
	(
		cd downstream &&
		but config fetch.recurseSubmodules false &&
		(
			cd submodule &&
			but config -f .butmodules submodule.subdir/deepsubmodule.fetchRecursive false
		) &&
		but fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err &&
		but config --unset fetch.recurseSubmodules &&
		(
			cd submodule &&
			but config --unset -f .butmodules submodule.subdir/deepsubmodule.fetchRecursive
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
		but checkout --recurse-submodules -b no-submodules &&
		but rm .butmodules &&
		but rm submodule &&
		but cummit -m "no submodules" &&
		but checkout --recurse-submodules super
	)
'

test_expect_success "'--recurse-submodules=on-demand' should fetch submodule cummits if the submodule is changed but the index has no submodules" '
	add_submodule_cummits &&
	add_superproject_cummits &&
	# Fetch the new superproject cummit
	(
		cd downstream &&
		but switch --recurse-submodules no-submodules &&
		but fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err
	) &&
	super_head=$(but rev-parse --short HEAD) &&
	sub_head=$(but -C submodule rev-parse --short HEAD) &&
	deep_head=$(but -C submodule/subdir/deepsubmodule rev-parse --short HEAD) &&

	# assert that these are fetched from cummits, not the index
	write_expected_sub $sub_head $super_head &&
	write_expected_deep $deep_head $sub_head &&

	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "'--recurse-submodules' should fetch submodule cummits if the submodule is changed but the index has no submodules" '
	add_submodule_cummits &&
	add_superproject_cummits &&
	# Fetch the new superproject cummit
	(
		cd downstream &&
		but switch --recurse-submodules no-submodules &&
		but fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	super_head=$(but rev-parse --short HEAD) &&
	sub_head=$(but -C submodule rev-parse --short HEAD) &&
	deep_head=$(but -C submodule/subdir/deepsubmodule rev-parse --short HEAD) &&

	# assert that these are fetched from cummits, not the index
	write_expected_sub $sub_head $super_head &&
	write_expected_deep $deep_head $sub_head &&

	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "'--recurse-submodules' should ignore changed, inactive submodules" '
	add_submodule_cummits &&
	add_superproject_cummits &&

	# Fetch the new superproject cummit
	(
		cd downstream &&
		but switch --recurse-submodules no-submodules &&
		but -c submodule.submodule.active=false fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	super_head=$(but rev-parse --short HEAD) &&
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
		but init &&
		echo sub2content >sub2file &&
		but add sub2file &&
		but cummit -a -m new &&
		but branch -M sub2
	) &&
	but checkout -b super-sub2-only &&
	but submodule add "$pwd/submodule2" submodule2 &&
	but cummit -m "add sub2" &&
	but checkout super &&
	(
		cd downstream &&
		but fetch --recurse-submodules origin &&
		but checkout super-sub2-only &&
		# Explicitly run "but submodule update" because sub2 is new
		# and has not been cloned.
		but submodule update --init &&
		but checkout --recurse-submodules super
	)
'

test_expect_success "'--recurse-submodules' should fetch submodule cummits in changed submodules and the index" '
	test_when_finished "rm expect.err.sub2" &&
	# Create new cummit in origin/super
	add_submodule_cummits &&
	add_superproject_cummits &&

	# Create new cummit in origin/super-sub2-only
	but checkout super-sub2-only &&
	(
		cd submodule2 &&
		test_cummit --no-tag foo
	) &&
	but add submodule2 &&
	but cummit -m "new submodule2" &&

	but checkout super &&
	(
		cd downstream &&
		but fetch --recurse-submodules >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	sub2_head=$(but -C submodule2 rev-parse --short HEAD) &&
	super_head=$(but rev-parse --short super) &&
	super_sub2_only_head=$(but rev-parse --short super-sub2-only) &&
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

test_expect_success "'--recurse-submodules=on-demand' stops when no new submodule cummits are found in the superproject (and ignores config)" '
	add_submodule_cummits &&
	echo a >> file &&
	but add file &&
	but cummit -m "new file" &&
	new_head=$(but rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.sub &&
	rm expect.err.deep &&
	(
		cd downstream &&
		but fetch --recurse-submodules=on-demand >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "'fetch.recurseSubmodules=on-demand' overrides global config" '
	(
		cd downstream &&
		but fetch --recurse-submodules
	) &&
	add_submodule_cummits &&
	but config --global fetch.recurseSubmodules false &&
	but add submodule &&
	but cummit -m "new submodule" &&
	new_head=$(but rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.deep &&
	(
		cd downstream &&
		but config fetch.recurseSubmodules on-demand &&
		but fetch >../actual.out 2>../actual.err
	) &&
	but config --global --unset fetch.recurseSubmodules &&
	(
		cd downstream &&
		but config --unset fetch.recurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "'submodule.<sub>.fetchRecurseSubmodules=on-demand' overrides fetch.recurseSubmodules" '
	(
		cd downstream &&
		but fetch --recurse-submodules
	) &&
	add_submodule_cummits &&
	but config fetch.recurseSubmodules false &&
	but add submodule &&
	but cummit -m "new submodule" &&
	new_head=$(but rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.deep &&
	(
		cd downstream &&
		but config submodule.submodule.fetchRecurseSubmodules on-demand &&
		but fetch >../actual.out 2>../actual.err
	) &&
	but config --unset fetch.recurseSubmodules &&
	(
		cd downstream &&
		but config --unset submodule.submodule.fetchRecurseSubmodules
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err
'

test_expect_success "don't fetch submodule when newly recorded cummits are already present" '
	(
		cd submodule &&
		but checkout -q HEAD^^
	) &&
	but add submodule &&
	but cummit -m "submodule rewound" &&
	new_head=$(but rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.sub &&
	# This file does not exist, but rm -f for readability
	rm -f expect.err.deep &&
	(
		cd downstream &&
		but fetch >../actual.out 2>../actual.err
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err &&
	(
		cd submodule &&
		but checkout -q sub
	)
'

test_expect_success "'fetch.recurseSubmodules=on-demand' works also without .butmodules entry" '
	(
		cd downstream &&
		but fetch --recurse-submodules
	) &&
	add_submodule_cummits &&
	but add submodule &&
	but rm .butmodules &&
	but cummit -m "new submodule without .butmodules" &&
	new_head=$(but rev-parse --short HEAD) &&
	write_expected_super $new_head &&
	rm expect.err.deep &&
	(
		cd downstream &&
		rm .butmodules &&
		but config fetch.recurseSubmodules on-demand &&
		# fake submodule configuration to avoid skipping submodule handling
		but config -f .butmodules submodule.fake.path fake &&
		but config -f .butmodules submodule.fake.url fakeurl &&
		but add .butmodules &&
		but config --unset submodule.submodule.url &&
		but fetch >../actual.out 2>../actual.err &&
		# cleanup
		but config --unset fetch.recurseSubmodules &&
		but reset --hard
	) &&
	test_must_be_empty actual.out &&
	verify_fetch_result actual.err &&
	but checkout HEAD^ -- .butmodules &&
	but add .butmodules &&
	but cummit -m "new submodule restored .butmodules"
'

test_expect_success 'fetching submodules respects parallel settings' '
	but config fetch.recurseSubmodules true &&
	(
		cd downstream &&
		GIT_TRACE=$(pwd)/trace.out but fetch &&
		grep "1 tasks" trace.out &&
		GIT_TRACE=$(pwd)/trace.out but fetch --jobs 7 &&
		grep "7 tasks" trace.out &&
		but config submodule.fetchJobs 8 &&
		GIT_TRACE=$(pwd)/trace.out but fetch &&
		grep "8 tasks" trace.out &&
		GIT_TRACE=$(pwd)/trace.out but fetch --jobs 9 &&
		grep "9 tasks" trace.out
	)
'

test_expect_success 'fetching submodule into a broken repository' '
	# Prepare src and src/sub nested in it
	but init src &&
	(
		cd src &&
		but init sub &&
		but -C sub cummit --allow-empty -m "initial in sub" &&
		but submodule add -- ./sub sub &&
		but cummit -m "initial in top"
	) &&

	# Clone the old-fashoned way
	but clone src dst &&
	but -C dst clone ../src/sub sub &&

	# Make sure that old-fashoned layout is still supported
	but -C dst status &&

	# "diff" would find no change
	but -C dst diff --exit-code &&

	# Recursive-fetch works fine
	but -C dst fetch --recurse-submodules &&

	# Break the receiving submodule
	rm -f dst/sub/.but/HEAD &&

	# NOTE: without the fix the following tests will recurse forever!
	# They should terminate with an error.

	test_must_fail but -C dst status &&
	test_must_fail but -C dst diff &&
	test_must_fail but -C dst fetch --recurse-submodules
'

test_expect_success "fetch new cummits when submodule got renamed" '
	but clone . downstream_rename &&
	(
		cd downstream_rename &&
		but submodule update --init --recursive &&
		but checkout -b rename &&
		but mv submodule submodule_renamed &&
		(
			cd submodule_renamed &&
			but checkout -b rename_sub &&
			echo a >a &&
			but add a &&
			but cummit -ma &&
			but push origin rename_sub &&
			but rev-parse HEAD >../../expect
		) &&
		but add submodule_renamed &&
		but cummit -m "update renamed submodule" &&
		but push origin rename
	) &&
	(
		cd downstream &&
		but fetch --recurse-submodules=on-demand &&
		(
			cd submodule &&
			but rev-parse origin/rename_sub >../../actual
		)
	) &&
	test_cmp expect actual
'

test_expect_success "fetch new submodule cummits on-demand outside standard refspec" '
	# add a second submodule and ensure it is around in downstream first
	but clone submodule sub1 &&
	but submodule add ./sub1 &&
	but cummit -m "adding a second submodule" &&
	but -C downstream pull &&
	but -C downstream submodule update --init --recursive &&

	but checkout --detach &&

	C=$(but -C submodule cummit-tree -m "new change outside refs/heads" HEAD^{tree}) &&
	but -C submodule update-ref refs/changes/1 $C &&
	but update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(but -C sub1 cummit-tree -m "new change outside refs/heads" HEAD^{tree}) &&
	but -C sub1 update-ref refs/changes/2 $D &&
	but update-index --cacheinfo 160000 $D sub1 &&

	but cummit -m "updated submodules outside of refs/heads" &&
	E=$(but rev-parse HEAD) &&
	but update-ref refs/changes/3 $E &&
	(
		cd downstream &&
		but fetch --recurse-submodules origin refs/changes/3:refs/heads/my_branch &&
		but -C submodule cat-file -t $C &&
		but -C sub1 cat-file -t $D &&
		but checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule cummit on-demand in FETCH_HEAD' '
	# depends on the previous test for setup

	C=$(but -C submodule cummit-tree -m "another change outside refs/heads" HEAD^{tree}) &&
	but -C submodule update-ref refs/changes/4 $C &&
	but update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(but -C sub1 cummit-tree -m "another change outside refs/heads" HEAD^{tree}) &&
	but -C sub1 update-ref refs/changes/5 $D &&
	but update-index --cacheinfo 160000 $D sub1 &&

	but cummit -m "updated submodules outside of refs/heads" &&
	E=$(but rev-parse HEAD) &&
	but update-ref refs/changes/6 $E &&
	(
		cd downstream &&
		but fetch --recurse-submodules origin refs/changes/6 &&
		but -C submodule cat-file -t $C &&
		but -C sub1 cat-file -t $D &&
		but checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule cummits on-demand without .butmodules entry' '
	# depends on the previous test for setup

	but config -f .butmodules --remove-section submodule.sub1 &&
	but add .butmodules &&
	but cummit -m "delete butmodules file" &&
	but checkout -B super &&
	but -C downstream fetch &&
	but -C downstream checkout origin/super &&

	C=$(but -C submodule cummit-tree -m "yet another change outside refs/heads" HEAD^{tree}) &&
	but -C submodule update-ref refs/changes/7 $C &&
	but update-index --cacheinfo 160000 $C submodule &&
	test_tick &&

	D=$(but -C sub1 cummit-tree -m "yet another change outside refs/heads" HEAD^{tree}) &&
	but -C sub1 update-ref refs/changes/8 $D &&
	but update-index --cacheinfo 160000 $D sub1 &&

	but cummit -m "updated submodules outside of refs/heads" &&
	E=$(but rev-parse HEAD) &&
	but update-ref refs/changes/9 $E &&
	(
		cd downstream &&
		but fetch --recurse-submodules origin refs/changes/9 &&
		but -C submodule cat-file -t $C &&
		but -C sub1 cat-file -t $D &&
		but checkout --recurse-submodules FETCH_HEAD
	)
'

test_expect_success 'fetch new submodule cummit intermittently referenced by superproject' '
	# depends on the previous test for setup

	D=$(but -C sub1 cummit-tree -m "change 10 outside refs/heads" HEAD^{tree}) &&
	E=$(but -C sub1 cummit-tree -m "change 11 outside refs/heads" HEAD^{tree}) &&
	F=$(but -C sub1 cummit-tree -m "change 12 outside refs/heads" HEAD^{tree}) &&

	but -C sub1 update-ref refs/changes/10 $D &&
	but update-index --cacheinfo 160000 $D sub1 &&
	but cummit -m "updated submodules outside of refs/heads" &&

	but -C sub1 update-ref refs/changes/11 $E &&
	but update-index --cacheinfo 160000 $E sub1 &&
	but cummit -m "updated submodules outside of refs/heads" &&

	but -C sub1 update-ref refs/changes/12 $F &&
	but update-index --cacheinfo 160000 $F sub1 &&
	but cummit -m "updated submodules outside of refs/heads" &&

	G=$(but rev-parse HEAD) &&
	but update-ref refs/changes/13 $G &&
	(
		cd downstream &&
		but fetch --recurse-submodules origin refs/changes/13 &&

		but -C sub1 cat-file -t $D &&
		but -C sub1 cat-file -t $E &&
		but -C sub1 cat-file -t $F
	)
'

add_cummit_push () {
	dir="$1" &&
	msg="$2" &&
	shift 2 &&
	but -C "$dir" add "$@" &&
	but -C "$dir" cummit -a -m "$msg" &&
	but -C "$dir" push
}

compare_refs_in_dir () {
	fail= &&
	if test "x$1" = 'x!'
	then
		fail='!' &&
		shift
	fi &&
	but -C "$1" rev-parse --verify "$2" >expect &&
	but -C "$3" rev-parse --verify "$4" >actual &&
	eval $fail test_cmp expect actual
}


test_expect_success 'setup nested submodule fetch test' '
	# does not depend on any previous test setups

	for repo in outer middle inner
	do
		but init --bare $repo &&
		but clone $repo ${repo}_content &&
		echo "$repo" >"${repo}_content/file" &&
		add_cummit_push ${repo}_content "initial" file ||
		return 1
	done &&

	but clone outer A &&
	but -C A submodule add "$pwd/middle" &&
	but -C A/middle/ submodule add "$pwd/inner" &&
	add_cummit_push A/middle/ "adding inner sub" .butmodules inner &&
	add_cummit_push A/ "adding middle sub" .butmodules middle &&

	but clone outer B &&
	but -C B/ submodule update --init middle &&

	compare_refs_in_dir A HEAD B HEAD &&
	compare_refs_in_dir A/middle HEAD B/middle HEAD &&
	test_path_is_file B/file &&
	test_path_is_file B/middle/file &&
	test_path_is_missing B/middle/inner/file &&

	echo "change on inner repo of A" >"A/middle/inner/file" &&
	add_cummit_push A/middle/inner "change on inner" file &&
	add_cummit_push A/middle "change on inner" inner &&
	add_cummit_push A "change on inner" middle
'

test_expect_success 'fetching a superproject containing an uninitialized sub/sub project' '
	# depends on previous test for setup

	but -C B/ fetch &&
	compare_refs_in_dir A origin/HEAD B origin/HEAD
'

fetch_with_recursion_abort () {
	# In a regression the following but call will run into infinite recursion.
	# To handle that, we connect the sed command to the but call by a pipe
	# so that sed can kill the infinite recursion when detected.
	# The recursion creates but output like:
	# Fetching submodule sub
	# Fetching submodule sub/sub              <-- [1]
	# Fetching submodule sub/sub/sub
	# ...
	# [1] sed will stop reading and cause but to eventually stop and die

	but -C "$1" fetch --recurse-submodules 2>&1 |
		sed "/Fetching submodule $2[^$]/q" >out &&
	! grep "Fetching submodule $2[^$]" out
}

test_expect_success 'setup recursive fetch with uninit submodule' '
	# does not depend on any previous test setups

	test_create_repo super &&
	test_cummit -C super initial &&
	test_create_repo sub &&
	test_cummit -C sub initial &&
	but -C sub rev-parse HEAD >expect &&

	but -C super submodule add ../sub &&
	but -C super cummit -m "add sub" &&

	but clone super superclone &&
	but -C superclone submodule status >out &&
	sed -e "s/^-//" -e "s/ sub.*$//" out >actual &&
	test_cmp expect actual
'

test_expect_success 'recursive fetch with uninit submodule' '
	# depends on previous test for setup

	fetch_with_recursion_abort superclone sub &&
	but -C superclone submodule status >out &&
	sed -e "s/^-//" -e "s/ sub$//" out >actual &&
	test_cmp expect actual
'

test_expect_success 'recursive fetch after deinit a submodule' '
	# depends on previous test for setup

	but -C superclone submodule update --init sub &&
	but -C superclone submodule deinit -f sub &&

	fetch_with_recursion_abort superclone sub &&
	but -C superclone submodule status >out &&
	sed -e "s/^-//" -e "s/ sub$//" out >actual &&
	test_cmp expect actual
'

test_expect_success 'setup repo with upstreams that share a submodule name' '
	mkdir same-name-1 &&
	(
		cd same-name-1 &&
		but init -b main &&
		test_cummit --no-tag a
	) &&
	but clone same-name-1 same-name-2 &&
	# same-name-1 and same-name-2 both add a submodule with the
	# name "submodule"
	(
		cd same-name-1 &&
		mkdir submodule &&
		but -C submodule init -b main &&
		test_cummit -C submodule --no-tag a1 &&
		but submodule add "$pwd/same-name-1/submodule" &&
		but add submodule &&
		but cummit -m "super-a1"
	) &&
	(
		cd same-name-2 &&
		mkdir submodule &&
		but -C submodule init -b main &&
		test_cummit -C submodule --no-tag a2 &&
		but submodule add "$pwd/same-name-2/submodule" &&
		but add submodule &&
		but cummit -m "super-a2"
	) &&
	but clone same-name-1 -o same-name-1 same-name-downstream &&
	(
		cd same-name-downstream &&
		but remote add same-name-2 ../same-name-2 &&
		but fetch --all &&
		# init downstream with same-name-1
		but submodule update --init
	)
'

test_expect_success 'fetch --recurse-submodules updates name-conflicted, populated submodule' '
	test_when_finished "but -C same-name-downstream checkout main" &&
	(
		cd same-name-1 &&
		test_cummit -C submodule --no-tag b1 &&
		but add submodule &&
		but cummit -m "super-b1"
	) &&
	(
		cd same-name-2 &&
		test_cummit -C submodule --no-tag b2 &&
		but add submodule &&
		but cummit -m "super-b2"
	) &&
	(
		cd same-name-downstream &&
		# even though the .butmodules is correct, we cannot
		# fetch from same-name-2
		but checkout same-name-2/main &&
		but fetch --recurse-submodules same-name-1 &&
		test_must_fail but fetch --recurse-submodules same-name-2
	) &&
	super_head1=$(but -C same-name-1 rev-parse HEAD) &&
	but -C same-name-downstream cat-file -e $super_head1 &&

	super_head2=$(but -C same-name-2 rev-parse HEAD) &&
	but -C same-name-downstream cat-file -e $super_head2 &&

	sub_head1=$(but -C same-name-1/submodule rev-parse HEAD) &&
	but -C same-name-downstream/submodule cat-file -e $sub_head1 &&

	sub_head2=$(but -C same-name-2/submodule rev-parse HEAD) &&
	test_must_fail but -C same-name-downstream/submodule cat-file -e $sub_head2
'

test_expect_success 'fetch --recurse-submodules updates name-conflicted, unpopulated submodule' '
	(
		cd same-name-1 &&
		test_cummit -C submodule --no-tag c1 &&
		but add submodule &&
		but cummit -m "super-c1"
	) &&
	(
		cd same-name-2 &&
		test_cummit -C submodule --no-tag c2 &&
		but add submodule &&
		but cummit -m "super-c2"
	) &&
	(
		cd same-name-downstream &&
		but checkout main &&
		but rm .butmodules &&
		but rm submodule &&
		but cummit -m "no submodules" &&
		but fetch --recurse-submodules same-name-1
	) &&
	head1=$(but -C same-name-1/submodule rev-parse HEAD) &&
	head2=$(but -C same-name-2/submodule rev-parse HEAD) &&
	(
		cd same-name-downstream/.but/modules/submodule &&
		# The submodule has core.worktree pointing to the "but
		# rm"-ed directory, overwrite the invalid value. See
		# comment in get_fetch_task_from_changed() for more
		# information.
		but --work-tree=. cat-file -e $head1 &&
		test_must_fail but --work-tree=. cat-file -e $head2
	)
'

test_done
