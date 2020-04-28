#!/bin/sh

test_description='pushing to a repository using the atomic push option'

. ./test-lib.sh

mk_repo_pair () {
	rm -rf workbench upstream &&
	test_create_repo upstream &&
	test_create_repo workbench &&
	(
		cd upstream &&
		git config receive.denyCurrentBranch warn
	) &&
	(
		cd workbench &&
		git remote add up ../upstream
	)
}

# Compare the ref ($1) in upstream with a ref value from workbench ($2)
# i.e. test_refs second HEAD@{2}
test_refs () {
	test $# = 2 &&
	git -C upstream rev-parse --verify "$1" >expect &&
	git -C workbench rev-parse --verify "$2" >actual &&
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
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		git push --atomic up master
	) &&
	test_refs master master
'

test_expect_success 'atomic push works for two branches' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_commit one &&
		git branch second &&
		git push --mirror up &&
		test_commit two &&
		git checkout second &&
		test_commit three &&
		git push --atomic up master second
	) &&
	test_refs master master &&
	test_refs second second
'

test_expect_success 'atomic push works in combination with --mirror' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_commit one &&
		git checkout -b second &&
		test_commit two &&
		git push --atomic --mirror up
	) &&
	test_refs master master &&
	test_refs second second
'

test_expect_success 'atomic push works in combination with --force' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_commit one &&
		git branch second master &&
		test_commit two_a &&
		git checkout second &&
		test_commit two_b &&
		test_commit three_b &&
		test_commit four &&
		git push --mirror up &&
		# The actual test is below
		git checkout master &&
		test_commit three_a &&
		git checkout second &&
		git reset --hard HEAD^ &&
		git push --force --atomic up master second
	) &&
	test_refs master master &&
	test_refs second second
'

# set up two branches where master can be pushed but second can not
# (non-fast-forward). Since second can not be pushed the whole operation
# will fail and leave master untouched.
test_expect_success 'atomic push fails if one branch fails' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_commit one &&
		git checkout -b second master &&
		test_commit two &&
		test_commit three &&
		test_commit four &&
		git push --mirror up &&
		git reset --hard HEAD~2 &&
		test_commit five &&
		git checkout master &&
		test_commit six &&
		test_must_fail git push --atomic --all up
	) &&
	test_refs master HEAD@{7} &&
	test_refs second HEAD@{4}
'

test_expect_success 'atomic push fails if one tag fails remotely' '
	# prepare the repo
	mk_repo_pair &&
	(
		cd workbench &&
		test_commit one &&
		git checkout -b second master &&
		test_commit two &&
		git push --mirror up
	) &&
	# a third party modifies the server side:
	(
		cd upstream &&
		git checkout second &&
		git tag test_tag second
	) &&
	# see if we can now push both branches.
	(
		cd workbench &&
		git checkout master &&
		test_commit three &&
		git checkout second &&
		test_commit four &&
		git tag test_tag &&
		test_must_fail git push --tags --atomic up master second
	) &&
	test_refs master HEAD@{3} &&
	test_refs second HEAD@{1}
'

test_expect_success 'atomic push obeys update hook preventing a branch to be pushed' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_commit one &&
		git checkout -b second master &&
		test_commit two &&
		git push --mirror up
	) &&
	(
		cd upstream &&
		HOOKDIR="$(git rev-parse --git-dir)/hooks" &&
		HOOK="$HOOKDIR/update" &&
		mkdir -p "$HOOKDIR" &&
		write_script "$HOOK" <<-\EOF
			# only allow update to master from now on
			test "$1" = "refs/heads/master"
		EOF
	) &&
	(
		cd workbench &&
		git checkout master &&
		test_commit three &&
		git checkout second &&
		test_commit four &&
		test_must_fail git push --atomic up master second
	) &&
	test_refs master HEAD@{3} &&
	test_refs second HEAD@{1}
'

test_expect_success 'atomic push is not advertised if configured' '
	mk_repo_pair &&
	(
		cd upstream &&
		git config receive.advertiseatomic 0
	) &&
	(
		cd workbench &&
		test_commit one &&
		git push --mirror up &&
		test_commit two &&
		test_must_fail git push --atomic up master
	) &&
	test_refs master HEAD@{1}
'

# References in upstream : master(1) one(1) foo(1)
# References in workbench: master(2)        foo(1) two(2) bar(2)
# Atomic push            : master(2)               two(2) bar(2)
test_expect_success 'atomic push reports (reject by update hook)' '
	mk_repo_pair &&
	(
		cd workbench &&
		test_commit one &&
		git branch foo &&
		git push up master one foo &&
		git tag -d one
	) &&
	(
		mkdir -p upstream/.git/hooks &&
		cat >upstream/.git/hooks/update <<-EOF &&
		#!/bin/sh

		if test "\$1" = "refs/heads/bar"
		then
			echo >&2 "Pusing to branch bar is prohibited"
			exit 1
		fi
		EOF
		chmod a+x upstream/.git/hooks/update
	) &&
	(
		cd workbench &&
		test_commit two &&
		git branch bar
	) &&
	test_must_fail git -C workbench \
		push --atomic up master two bar >out 2>&1 &&
	fmt_status_report <out >actual &&
	cat >expect <<-EOF &&
	To ../upstream
	 ! [remote rejected] master -> master (atomic push failure)
	 ! [remote rejected] two -> two (atomic push failure)
	 ! [remote rejected] bar -> bar (hook declined)
	EOF
	test_cmp expect actual
'

# References in upstream : master(1) one(1) foo(1)
# References in workbench: master(2)        foo(1) two(2) bar(2)
test_expect_success 'atomic push reports (mirror, but reject by update hook)' '
	(
		cd workbench &&
		git remote remove up &&
		git remote add up ../upstream
	) &&
	test_must_fail git -C workbench \
		push --atomic --mirror up >out 2>&1 &&
	fmt_status_report <out >actual &&
	cat >expect <<-EOF &&
	To ../upstream
	 ! [remote rejected] master -> master (atomic push failure)
	 ! [remote rejected] one (atomic push failure)
	 ! [remote rejected] bar -> bar (hook declined)
	 ! [remote rejected] two -> two (atomic push failure)
	EOF
	test_cmp expect actual
'

# References in upstream : master(2) one(1) foo(1)
# References in workbench: master(1)        foo(1) two(2) bar(2)
test_expect_success 'atomic push reports (reject by non-ff)' '
	rm upstream/.git/hooks/update &&
	(
		cd workbench &&
		git push up master &&
		git reset --hard HEAD^
	) &&
	test_must_fail git -C workbench \
		push --atomic up master foo bar >out 2>&1 &&
	fmt_status_report <out >actual &&
	cat >expect <<-EOF &&
	To ../upstream
	 ! [rejected] master -> master (non-fast-forward)
	 ! [rejected] bar -> bar (atomic push failed)
	EOF
	test_cmp expect actual
'

test_done
