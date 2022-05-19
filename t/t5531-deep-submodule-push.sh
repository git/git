#!/bin/sh

test_description='test push with submodules'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export GIT_TEST_FATAL_REGISTER_SUBMODULE_ODB

. ./test-lib.sh

test_expect_success setup '
	mkdir pub.but &&
	GIT_DIR=pub.but but init --bare &&
	GIT_DIR=pub.but but config receive.fsckobjects true &&
	mkdir work &&
	(
		cd work &&
		but init &&
		but config push.default matching &&
		mkdir -p gar/bage &&
		(
			cd gar/bage &&
			but init &&
			but config push.default matching &&
			>junk &&
			but add junk &&
			but cummit -m "Initial junk"
		) &&
		but add gar/bage &&
		but cummit -m "Initial superproject"
	)
'

test_expect_success 'push works with recorded butlink' '
	(
		cd work &&
		but push ../pub.but main
	)
'

test_expect_success 'push if submodule has no remote' '
	(
		cd work/gar/bage &&
		>junk2 &&
		but add junk2 &&
		but cummit -m "Second junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Second cummit for gar/bage" &&
		but push --recurse-submodules=check ../pub.but main
	)
'

test_expect_success 'push fails if submodule cummit not on remote' '
	(
		cd work/gar &&
		but clone --bare bage ../../submodule.but &&
		cd bage &&
		but remote add origin ../../../submodule.but &&
		but fetch &&
		>junk3 &&
		but add junk3 &&
		but cummit -m "Third junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Third cummit for gar/bage" &&
		# the push should fail with --recurse-submodules=check
		# on the command line...
		test_must_fail but push --recurse-submodules=check ../pub.but main &&

		# ...or if specified in the configuration..
		test_must_fail but -c push.recurseSubmodules=check push ../pub.but main
	)
'

test_expect_success 'push succeeds after cummit was pushed to remote' '
	(
		cd work/gar/bage &&
		but push origin main
	) &&
	(
		cd work &&
		but push --recurse-submodules=check ../pub.but main
	)
'

test_expect_success 'push succeeds if submodule cummit not on remote but using on-demand on command line' '
	(
		cd work/gar/bage &&
		>recurse-on-demand-on-command-line &&
		but add recurse-on-demand-on-command-line &&
		but cummit -m "Recurse on-demand on command line junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse on-demand on command line for gar/bage" &&
		but push --recurse-submodules=on-demand ../pub.but main &&
		# Check that the supermodule cummit got there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		# Check that the submodule cummit got there too
		cd gar/bage &&
		but diff --quiet origin/main main
	)
'

test_expect_success 'push succeeds if submodule cummit not on remote but using on-demand from config' '
	(
		cd work/gar/bage &&
		>recurse-on-demand-from-config &&
		but add recurse-on-demand-from-config &&
		but cummit -m "Recurse on-demand from config junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse on-demand from config for gar/bage" &&
		but -c push.recurseSubmodules=on-demand push ../pub.but main &&
		# Check that the supermodule cummit got there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		# Check that the submodule cummit got there too
		cd gar/bage &&
		but diff --quiet origin/main main
	)
'

test_expect_success 'push succeeds if submodule cummit not on remote but using auto-on-demand via submodule.recurse config' '
	(
		cd work/gar/bage &&
		>recurse-on-demand-from-submodule-recurse-config &&
		but add recurse-on-demand-from-submodule-recurse-config &&
		but cummit -m "Recurse submodule.recurse from config junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse submodule.recurse from config for gar/bage" &&
		but -c submodule.recurse push ../pub.but main &&
		# Check that the supermodule cummit got there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		# Check that the submodule cummit got there too
		cd gar/bage &&
		but diff --quiet origin/main main
	)
'

test_expect_success 'push recurse-submodules on command line overrides config' '
	(
		cd work/gar/bage &&
		>recurse-check-on-command-line-overriding-config &&
		but add recurse-check-on-command-line-overriding-config &&
		but cummit -m "Recurse on command-line overriding config junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse on command-line overriding config for gar/bage" &&

		# Ensure that we can override on-demand in the config
		# to just check submodules
		test_must_fail but -c push.recurseSubmodules=on-demand push --recurse-submodules=check ../pub.but main &&
		# Check that the supermodule cummit did not get there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main^ &&
		# Check that the submodule cummit did not get there
		(cd gar/bage && but diff --quiet origin/main main^) &&

		# Ensure that we can override check in the config to
		# disable submodule recursion entirely
		(cd gar/bage && but diff --quiet origin/main main^) &&
		but -c push.recurseSubmodules=on-demand push --recurse-submodules=no ../pub.but main &&
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		(cd gar/bage && but diff --quiet origin/main main^) &&

		# Ensure that we can override check in the config to
		# disable submodule recursion entirely (alternative form)
		but -c push.recurseSubmodules=on-demand push --no-recurse-submodules ../pub.but main &&
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		(cd gar/bage && but diff --quiet origin/main main^) &&

		# Ensure that we can override check in the config to
		# push the submodule too
		but -c push.recurseSubmodules=check push --recurse-submodules=on-demand ../pub.but main &&
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		(cd gar/bage && but diff --quiet origin/main main)
	)
'

test_expect_success 'push recurse-submodules last one wins on command line' '
	(
		cd work/gar/bage &&
		>recurse-check-on-command-line-overriding-earlier-command-line &&
		but add recurse-check-on-command-line-overriding-earlier-command-line &&
		but cummit -m "Recurse on command-line overridiing earlier command-line junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse on command-line overriding earlier command-line for gar/bage" &&

		# should result in "check"
		test_must_fail but push --recurse-submodules=on-demand --recurse-submodules=check ../pub.but main &&
		# Check that the supermodule cummit did not get there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main^ &&
		# Check that the submodule cummit did not get there
		(cd gar/bage && but diff --quiet origin/main main^) &&

		# should result in "no"
		but push --recurse-submodules=on-demand --recurse-submodules=no ../pub.but main &&
		# Check that the supermodule cummit did get there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		# Check that the submodule cummit did not get there
		(cd gar/bage && but diff --quiet origin/main main^) &&

		# should result in "no"
		but push --recurse-submodules=on-demand --no-recurse-submodules ../pub.but main &&
		# Check that the submodule cummit did not get there
		(cd gar/bage && but diff --quiet origin/main main^) &&

		# But the options in the other order should push the submodule
		but push --recurse-submodules=check --recurse-submodules=on-demand ../pub.but main &&
		# Check that the submodule cummit did get there
		but fetch ../pub.but &&
		(cd gar/bage && but diff --quiet origin/main main)
	)
'

test_expect_success 'push succeeds if submodule cummit not on remote using on-demand from cmdline overriding config' '
	(
		cd work/gar/bage &&
		>recurse-on-demand-on-command-line-overriding-config &&
		but add recurse-on-demand-on-command-line-overriding-config &&
		but cummit -m "Recurse on-demand on command-line overriding config junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse on-demand on command-line overriding config for gar/bage" &&
		but -c push.recurseSubmodules=check push --recurse-submodules=on-demand ../pub.but main &&
		# Check that the supermodule cummit got there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		# Check that the submodule cummit got there
		cd gar/bage &&
		but diff --quiet origin/main main
	)
'

test_expect_success 'push succeeds if submodule cummit disabling recursion from cmdline overriding config' '
	(
		cd work/gar/bage &&
		>recurse-disable-on-command-line-overriding-config &&
		but add recurse-disable-on-command-line-overriding-config &&
		but cummit -m "Recurse disable on command-line overriding config junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse disable on command-line overriding config for gar/bage" &&
		but -c push.recurseSubmodules=check push --recurse-submodules=no ../pub.but main &&
		# Check that the supermodule cummit got there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		# But that the submodule cummit did not
		( cd gar/bage && but diff --quiet origin/main main^ ) &&
		# Now push it to avoid confusing future tests
		but push --recurse-submodules=on-demand ../pub.but main
	)
'

test_expect_success 'push succeeds if submodule cummit disabling recursion from cmdline (alternative form) overriding config' '
	(
		cd work/gar/bage &&
		>recurse-disable-on-command-line-alt-overriding-config &&
		but add recurse-disable-on-command-line-alt-overriding-config &&
		but cummit -m "Recurse disable on command-line alternative overriding config junk"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse disable on command-line alternative overriding config for gar/bage" &&
		but -c push.recurseSubmodules=check push --no-recurse-submodules ../pub.but main &&
		# Check that the supermodule cummit got there
		but fetch ../pub.but &&
		but diff --quiet FETCH_HEAD main &&
		# But that the submodule cummit did not
		( cd gar/bage && but diff --quiet origin/main main^ ) &&
		# Now push it to avoid confusing future tests
		but push --recurse-submodules=on-demand ../pub.but main
	)
'

test_expect_success 'submodule entry pointing at a tag is error' '
	but -C work/gar/bage tag -a test1 -m "tag" &&
	tag=$(but -C work/gar/bage rev-parse test1^{tag}) &&
	but -C work update-index --cacheinfo 160000 "$tag" gar/bage &&
	but -C work cummit -m "bad cummit" &&
	test_when_finished "but -C work reset --hard HEAD^" &&
	test_must_fail but -C work push --recurse-submodules=on-demand ../pub.but main 2>err &&
	test_i18ngrep "is a tag, not a cummit" err
'

test_expect_success 'push fails if recurse submodules option passed as yes' '
	(
		cd work/gar/bage &&
		>recurse-push-fails-if-recurse-submodules-passed-as-yes &&
		but add recurse-push-fails-if-recurse-submodules-passed-as-yes &&
		but cummit -m "Recurse push fails if recurse submodules option passed as yes"
	) &&
	(
		cd work &&
		but add gar/bage &&
		but cummit -m "Recurse push fails if recurse submodules option passed as yes for gar/bage" &&
		test_must_fail but push --recurse-submodules=yes ../pub.but main &&
		test_must_fail but -c push.recurseSubmodules=yes push ../pub.but main &&
		but push --recurse-submodules=on-demand ../pub.but main
	)
'

test_expect_success 'push fails when cummit on multiple branches if one branch has no remote' '
	(
		cd work/gar/bage &&
		>junk4 &&
		but add junk4 &&
		but cummit -m "Fourth junk"
	) &&
	(
		cd work &&
		but branch branch2 &&
		but add gar/bage &&
		but cummit -m "Fourth cummit for gar/bage" &&
		but checkout branch2 &&
		(
			cd gar/bage &&
			but checkout HEAD~1
		) &&
		>junk1 &&
		but add junk1 &&
		but cummit -m "First junk" &&
		test_must_fail but push --recurse-submodules=check ../pub.but
	)
'

test_expect_success 'push succeeds if submodule has no remote and is on the first superproject cummit' '
	but init --bare a &&
	but clone a a1 &&
	(
		cd a1 &&
		but init b &&
		(
			cd b &&
			>junk &&
			but add junk &&
			but cummit -m "initial"
		) &&
		but add b &&
		but cummit -m "added submodule" &&
		but push --recurse-submodules=check origin main
	)
'

test_expect_success 'push unpushed submodules when not needed' '
	(
		cd work &&
		(
			cd gar/bage &&
			but checkout main &&
			>junk5 &&
			but add junk5 &&
			but cummit -m "Fifth junk" &&
			but push &&
			but rev-parse origin/main >../../../expected
		) &&
		but checkout main &&
		but add gar/bage &&
		but cummit -m "Fifth cummit for gar/bage" &&
		but push --recurse-submodules=on-demand ../pub.but main
	) &&
	(
		cd submodule.but &&
		but rev-parse main >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'push unpushed submodules when not needed 2' '
	(
		cd submodule.but &&
		but rev-parse main >../expected
	) &&
	(
		cd work &&
		(
			cd gar/bage &&
			>junk6 &&
			but add junk6 &&
			but cummit -m "Sixth junk"
		) &&
		>junk2 &&
		but add junk2 &&
		but cummit -m "Second junk for work" &&
		but push --recurse-submodules=on-demand ../pub.but main
	) &&
	(
		cd submodule.but &&
		but rev-parse main >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'push unpushed submodules recursively' '
	(
		cd work &&
		(
			cd gar/bage &&
			but checkout main &&
			> junk7 &&
			but add junk7 &&
			but cummit -m "Seventh junk" &&
			but rev-parse main >../../../expected
		) &&
		but checkout main &&
		but add gar/bage &&
		but cummit -m "Seventh cummit for gar/bage" &&
		but push --recurse-submodules=on-demand ../pub.but main
	) &&
	(
		cd submodule.but &&
		but rev-parse main >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'push unpushable submodule recursively fails' '
	(
		cd work &&
		(
			cd gar/bage &&
			but rev-parse origin/main >../../../expected &&
			but checkout main~0 &&
			> junk8 &&
			but add junk8 &&
			but cummit -m "Eighth junk"
		) &&
		but add gar/bage &&
		but cummit -m "Eighth cummit for gar/bage" &&
		test_must_fail but push --recurse-submodules=on-demand ../pub.but main
	) &&
	(
		cd submodule.but &&
		but rev-parse main >../actual
	) &&
	test_when_finished but -C work reset --hard main^ &&
	test_cmp expected actual
'

test_expect_success 'push --dry-run does not recursively update submodules' '
	(
		cd work/gar/bage &&
		but checkout main &&
		but rev-parse main >../../../expected_submodule &&
		> junk9 &&
		but add junk9 &&
		but cummit -m "Ninth junk" &&

		# Go up to 'work' directory
		cd ../.. &&
		but checkout main &&
		but rev-parse main >../expected_pub &&
		but add gar/bage &&
		but cummit -m "Ninth cummit for gar/bage" &&
		but push --dry-run --recurse-submodules=on-demand ../pub.but main
	) &&
	but -C submodule.but rev-parse main >actual_submodule &&
	but -C pub.but rev-parse main >actual_pub &&
	test_cmp expected_pub actual_pub &&
	test_cmp expected_submodule actual_submodule
'

test_expect_success 'push --dry-run does not recursively update submodules' '
	but -C work push --dry-run --recurse-submodules=only ../pub.but main &&

	but -C submodule.but rev-parse main >actual_submodule &&
	but -C pub.but rev-parse main >actual_pub &&
	test_cmp expected_pub actual_pub &&
	test_cmp expected_submodule actual_submodule
'

test_expect_success 'push only unpushed submodules recursively' '
	but -C work/gar/bage rev-parse main >expected_submodule &&
	but -C pub.but rev-parse main >expected_pub &&

	but -C work push --recurse-submodules=only ../pub.but main &&

	but -C submodule.but rev-parse main >actual_submodule &&
	but -C pub.but rev-parse main >actual_pub &&
	test_cmp expected_submodule actual_submodule &&
	test_cmp expected_pub actual_pub
'

test_expect_success 'push propagating the remotes name to a submodule' '
	but -C work remote add origin ../pub.but &&
	but -C work remote add pub ../pub.but &&

	> work/gar/bage/junk10 &&
	but -C work/gar/bage add junk10 &&
	but -C work/gar/bage cummit -m "Tenth junk" &&
	but -C work add gar/bage &&
	but -C work cummit -m "Tenth junk added to gar/bage" &&

	# Fails when submodule does not have a matching remote
	test_must_fail but -C work push --recurse-submodules=on-demand pub main &&
	# Succeeds when submodules has matching remote and refspec
	but -C work push --recurse-submodules=on-demand origin main &&

	but -C submodule.but rev-parse main >actual_submodule &&
	but -C pub.but rev-parse main >actual_pub &&
	but -C work/gar/bage rev-parse main >expected_submodule &&
	but -C work rev-parse main >expected_pub &&
	test_cmp expected_submodule actual_submodule &&
	test_cmp expected_pub actual_pub
'

test_expect_success 'push propagating refspec to a submodule' '
	> work/gar/bage/junk11 &&
	but -C work/gar/bage add junk11 &&
	but -C work/gar/bage cummit -m "Eleventh junk" &&

	but -C work checkout branch2 &&
	but -C work add gar/bage &&
	but -C work cummit -m "updating gar/bage in branch2" &&

	# Fails when submodule does not have a matching branch
	test_must_fail but -C work push --recurse-submodules=on-demand origin branch2 &&
	# Fails when refspec includes an object id
	test_must_fail but -C work push --recurse-submodules=on-demand origin \
		"$(but -C work rev-parse branch2):refs/heads/branch2" &&
	# Fails when refspec includes HEAD and parent and submodule do not
	# have the same named branch checked out
	test_must_fail but -C work push --recurse-submodules=on-demand origin \
		HEAD:refs/heads/branch2 &&

	but -C work/gar/bage branch branch2 main &&
	but -C work push --recurse-submodules=on-demand origin branch2 &&

	but -C submodule.but rev-parse branch2 >actual_submodule &&
	but -C pub.but rev-parse branch2 >actual_pub &&
	but -C work/gar/bage rev-parse branch2 >expected_submodule &&
	but -C work rev-parse branch2 >expected_pub &&
	test_cmp expected_submodule actual_submodule &&
	test_cmp expected_pub actual_pub
'

test_expect_success 'push propagating HEAD refspec to a submodule' '
	but -C work/gar/bage checkout branch2 &&
	> work/gar/bage/junk12 &&
	but -C work/gar/bage add junk12 &&
	but -C work/gar/bage cummit -m "Twelfth junk" &&

	but -C work checkout branch2 &&
	but -C work add gar/bage &&
	but -C work cummit -m "updating gar/bage in branch2" &&

	# Passes since the superproject and submodules HEAD are both on branch2
	but -C work push --recurse-submodules=on-demand origin \
		HEAD:refs/heads/branch2 &&

	but -C submodule.but rev-parse branch2 >actual_submodule &&
	but -C pub.but rev-parse branch2 >actual_pub &&
	but -C work/gar/bage rev-parse branch2 >expected_submodule &&
	but -C work rev-parse branch2 >expected_pub &&
	test_cmp expected_submodule actual_submodule &&
	test_cmp expected_pub actual_pub
'

test_done
