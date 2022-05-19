#!/bin/sh

test_description='pushing to a repository using the atomic push option'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

mk_repo_pair () {
	rm -rf workbench upstream &&
	test_create_repo upstream &&
	test_create_repo workbench &&
	(
		cd upstream &&
		but config receive.denyCurrentBranch warn
	) &&
	(
		cd workbench &&
		but remote add up ../upstream
	)
}

# Compare the ref ($1) in upstream with a ref value from workbench ($2)
# i.e. test_refs second HEAD@{2}
test_refs () {
	test $# = 2 &&
	but -C upstream rev-parse --verify "$1" >expect &&
	but -C workbench rev-parse --verify "$2" >actual &&
	test_cmp expect actual
}

fmt_status_report () {
	sed -n \
		-e "/^To / { s/   */ /g; p; }" \
		-e "/^ ! / { s/   */ /g; p; }"
}

test_expect_success 'atomic push works for a single branch' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		but push --atomic up main
	) &&
	test_refs main main
'

test_expect_success 'atomic push works for two branches' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_cummit one &&
		but branch second &&
		but push --mirror up &&
		test_cummit two &&
		but checkout second &&
		test_cummit three &&
		but push --atomic up main second
	) &&
	test_refs main main &&
	test_refs second second
'

test_expect_success 'atomic push works in combination with --mirror' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_cummit one &&
		but checkout -b second &&
		test_cummit two &&
		but push --atomic --mirror up
	) &&
	test_refs main main &&
	test_refs second second
'

test_expect_success 'atomic push works in combination with --force' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_cummit one &&
		but branch second main &&
		test_cummit two_a &&
		but checkout second &&
		test_cummit two_b &&
		test_cummit three_b &&
		test_cummit four &&
		but push --mirror up &&
		# The actual test is below
		but checkout main &&
		test_cummit three_a &&
		but checkout second &&
		but reset --hard HEAD^ &&
		but push --force --atomic up main second
	) &&
	test_refs main main &&
	test_refs second second
'

# set up two branches where main can be pushed but second can not
# (non-fast-forward). Since second can not be pushed the whole operation
# will fail and leave main untouched.
test_expect_success 'atomic push fails if one branch fails' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_cummit one &&
		but checkout -b second main &&
		test_cummit two &&
		test_cummit three &&
		test_cummit four &&
		but push --mirror up &&
		but reset --hard HEAD~2 &&
		test_cummit five &&
		but checkout main &&
		test_cummit six &&
		test_must_fail but push --atomic --all up
	) &&
	test_refs main HEAD@{7} &&
	test_refs second HEAD@{4}
'

test_expect_success 'atomic push fails if one tag fails remotely' '
	# prepare the repo
	mk_repo_pair &&
	(
		cd workbench &&
		test_cummit one &&
		but checkout -b second main &&
		test_cummit two &&
		but push --mirror up
	) &&
	# a third party modifies the server side:
	(
		cd upstream &&
		but checkout second &&
		but tag test_tag second
	) &&
	# see if we can now push both branches.
	(
		cd workbench &&
		but checkout main &&
		test_cummit three &&
		but checkout second &&
		test_cummit four &&
		but tag test_tag &&
		test_must_fail but push --tags --atomic up main second
	) &&
	test_refs main HEAD@{3} &&
	test_refs second HEAD@{1}
'

test_expect_success 'atomic push obeys update hook preventing a branch to be pushed' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_cummit one &&
		but checkout -b second main &&
		test_cummit two &&
		but push --mirror up
	) &&
	test_hook -C upstream update <<-\EOF &&
	# only allow update to main from now on
	test "$1" = "refs/heads/main"
	EOF
	(
		cd workbench &&
		but checkout main &&
		test_cummit three &&
		but checkout second &&
		test_cummit four &&
		test_must_fail but push --atomic up main second
	) &&
	test_refs main HEAD@{3} &&
	test_refs second HEAD@{1}
'

test_expect_success 'atomic push is not advertised if configured' '
	mk_repo_pair &&
	(
		cd upstream &&
		but config receive.advertiseatomic 0
	) &&
	(
		cd workbench &&
		test_cummit one &&
		but push --mirror up &&
		test_cummit two &&
		test_must_fail but push --atomic up main
	) &&
	test_refs main HEAD@{1}
'

# References in upstream : main(1) one(1) foo(1)
# References in workbench: main(2)        foo(1) two(2) bar(2)
# Atomic push            : main(2)               two(2) bar(2)
test_expect_success 'atomic push reports (reject by update hook)' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_cummit one &&
		but branch foo &&
		but push up main one foo &&
		but tag -d one
	) &&
	(
		mkdir -p upstream/.but/hooks &&
		cat >upstream/.but/hooks/update <<-EOF &&
		#!/bin/sh

		if test "\$1" = "refs/heads/bar"
		then
			echo >&2 "Pusing to branch bar is prohibited"
			exit 1
		fi
		EOF
		chmod a+x upstream/.but/hooks/update
	) &&
	(
		cd workbench &&
		test_cummit two &&
		but branch bar
	) &&
	test_must_fail but -C workbench \
		push --atomic up main two bar >out 2>&1 &&
	fmt_status_report <out >actual &&
	cat >expect <<-EOF &&
	To ../upstream
	 ! [remote rejected] main -> main (atomic push failure)
	 ! [remote rejected] two -> two (atomic push failure)
	 ! [remote rejected] bar -> bar (hook declined)
	EOF
	test_cmp expect actual
'

# References in upstream : main(1) one(1) foo(1)
# References in workbench: main(2)        foo(1) two(2) bar(2)
test_expect_success 'atomic push reports (mirror, but reject by update hook)' '
	(
		cd workbench &&
		but remote remove up &&
		but remote add up ../upstream
	) &&
	test_must_fail but -C workbench \
		push --atomic --mirror up >out 2>&1 &&
	fmt_status_report <out >actual &&
	cat >expect <<-EOF &&
	To ../upstream
	 ! [remote rejected] main -> main (atomic push failure)
	 ! [remote rejected] one (atomic push failure)
	 ! [remote rejected] bar -> bar (hook declined)
	 ! [remote rejected] two -> two (atomic push failure)
	EOF
	test_cmp expect actual
'

# References in upstream : main(2) one(1) foo(1)
# References in workbench: main(1)        foo(1) two(2) bar(2)
test_expect_success 'atomic push reports (reject by non-ff)' '
	rm upstream/.but/hooks/update &&
	(
		cd workbench &&
		but push up main &&
		but reset --hard HEAD^
	) &&
	test_must_fail but -C workbench \
		push --atomic up main foo bar >out 2>&1 &&
	fmt_status_report <out >actual &&
	cat >expect <<-EOF &&
	To ../upstream
	 ! [rejected] main -> main (non-fast-forward)
	 ! [rejected] bar -> bar (atomic push failed)
	EOF
	test_cmp expect actual
'

test_done
