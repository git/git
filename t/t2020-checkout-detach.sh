#!/bin/sh

test_description='checkout into detached HEAD state'
. ./test-lib.sh

check_detached () {
	test_must_fail git symbolic-ref -q HEAD >/dev/null
}

check_not_detached () {
	git symbolic-ref -q HEAD >/dev/null
}

PREV_HEAD_DESC='Previous HEAD position was'
check_orphan_warning() {
	test_i18ngrep "you are leaving $2 behind" "$1" &&
	test_i18ngrep ! "$PREV_HEAD_DESC" "$1"
}
check_no_orphan_warning() {
	test_i18ngrep ! "you are leaving .* commit.*behind" "$1" &&
	test_i18ngrep "$PREV_HEAD_DESC" "$1"
}

reset () {
	git checkout master &&
	check_not_detached
}

test_expect_success 'setup' '
	test_commit one &&
	test_commit two &&
	test_commit three && git tag -d three &&
	test_commit four && git tag -d four &&
	git branch branch &&
	git tag tag
'

test_expect_success 'checkout branch does not detach' '
	reset &&
	git checkout branch &&
	check_not_detached
'

test_expect_success 'checkout tag detaches' '
	reset &&
	git checkout tag &&
	check_detached
'

test_expect_success 'checkout branch by full name detaches' '
	reset &&
	git checkout refs/heads/branch &&
	check_detached
'

test_expect_success 'checkout non-ref detaches' '
	reset &&
	git checkout branch^ &&
	check_detached
'

test_expect_success 'checkout ref^0 detaches' '
	reset &&
	git checkout branch^0 &&
	check_detached
'

test_expect_success 'checkout --detach detaches' '
	reset &&
	git checkout --detach branch &&
	check_detached
'

test_expect_success 'checkout --detach without branch name' '
	reset &&
	git checkout --detach &&
	check_detached
'

test_expect_success 'checkout --detach errors out for non-commit' '
	reset &&
	test_must_fail git checkout --detach one^{tree} &&
	check_not_detached
'

test_expect_success 'checkout --detach errors out for extra argument' '
	reset &&
	git checkout master &&
	test_must_fail git checkout --detach tag one.t &&
	check_not_detached
'

test_expect_success 'checkout --detached and -b are incompatible' '
	reset &&
	test_must_fail git checkout --detach -b newbranch tag &&
	check_not_detached
'

test_expect_success 'checkout --detach moves HEAD' '
	reset &&
	git checkout one &&
	git checkout --detach two &&
	git diff --exit-code HEAD &&
	git diff --exit-code two
'

test_expect_success 'checkout warns on orphan commits' '
	reset &&
	git checkout --detach two &&
	echo content >orphan &&
	git add orphan &&
	git commit -a -m orphan1 &&
	echo new content >orphan &&
	git commit -a -m orphan2 &&
	orphan2=$(git rev-parse HEAD) &&
	git checkout master 2>stderr
'

test_expect_success 'checkout warns on orphan commits: output' '
	check_orphan_warning stderr "2 commits"
'

test_expect_success 'checkout warns orphaning 1 of 2 commits' '
	git checkout "$orphan2" &&
	git checkout HEAD^ 2>stderr
'

test_expect_success 'checkout warns orphaning 1 of 2 commits: output' '
	check_orphan_warning stderr "1 commit"
'

test_expect_success 'checkout does not warn leaving ref tip' '
	reset &&
	git checkout --detach two &&
	git checkout master 2>stderr
'

test_expect_success 'checkout does not warn leaving ref tip' '
	check_no_orphan_warning stderr
'

test_expect_success 'checkout does not warn leaving reachable commit' '
	reset &&
	git checkout --detach HEAD^ &&
	git checkout master 2>stderr
'

test_expect_success 'checkout does not warn leaving reachable commit' '
	check_no_orphan_warning stderr
'

cat >expect <<'EOF'
Your branch is behind 'master' by 1 commit, and can be fast-forwarded.
  (use "git pull" to update your local branch)
EOF
test_expect_success 'tracking count is accurate after orphan check' '
	reset &&
	git branch child master^ &&
	git config branch.child.remote . &&
	git config branch.child.merge refs/heads/master &&
	git checkout child^ &&
	git checkout child >stdout &&
	test_i18ncmp expect stdout
'

test_expect_success 'no advice given for explicit detached head state' '
	# baseline
	test_config advice.detachedHead true &&
	git checkout child && git checkout HEAD^0 >expect.advice 2>&1 &&
	test_config advice.detachedHead false &&
	git checkout child && git checkout HEAD^0 >expect.no-advice 2>&1 &&
	test_unconfig advice.detachedHead &&
	# without configuration, the advice.* variables default to true
	git checkout child && git checkout HEAD^0 >actual 2>&1 &&
	test_cmp expect.advice actual &&

	# with explicit --detach
	# no configuration
	test_unconfig advice.detachedHead &&
	git checkout child && git checkout --detach HEAD^0 >actual 2>&1 &&
	test_cmp expect.no-advice actual &&

	# explicitly decline advice
	test_config advice.detachedHead false &&
	git checkout child && git checkout --detach HEAD^0 >actual 2>&1 &&
	test_cmp expect.no-advice actual
'

# Detached HEAD tests for GIT_PRINT_SHA1_ELLIPSIS (new format)
test_expect_success 'describe_detached_head prints no SHA-1 ellipsis when not asked to' "

	commit=$(git rev-parse --short=12 master^) &&
	commit2=$(git rev-parse --short=12 master~2) &&
	commit3=$(git rev-parse --short=12 master~3) &&

	# The first detach operation is more chatty than the following ones.
	cat >1st_detach <<-EOF &&
	Note: checking out 'HEAD^'.

	You are in 'detached HEAD' state. You can look around, make experimental
	changes and commit them, and you can discard any commits you make in this
	state without impacting any branches by performing another checkout.

	If you want to create a new branch to retain commits you create, you may
	do so (now or later) by using -b with the checkout command again. Example:

	  git checkout -b <new-branch-name>

	HEAD is now at \$commit three
	EOF

	# The remaining ones just show info about previous and current HEADs.
	cat >2nd_detach <<-EOF &&
	Previous HEAD position was \$commit three
	HEAD is now at \$commit2 two
	EOF

	cat >3rd_detach <<-EOF &&
	Previous HEAD position was \$commit2 two
	HEAD is now at \$commit3 one
	EOF

	reset &&
	check_not_detached &&

	# Various ways of *not* asking for ellipses

	sane_unset GIT_PRINT_SHA1_ELLIPSIS &&
	git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 1st_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS="no" git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 2nd_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS= git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 3rd_detach actual &&

	sane_unset GIT_PRINT_SHA1_ELLIPSIS &&

	# We only have four commits, but we can re-use them
	reset &&
	check_not_detached &&

	# Make no mention of the env var at all
	git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 1st_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS='nope' &&
	git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 2nd_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS=nein &&
	git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 3rd_detach actual &&

	true
"

# Detached HEAD tests for GIT_PRINT_SHA1_ELLIPSIS (old format)
test_expect_success 'describe_detached_head does print SHA-1 ellipsis when asked to' "

	commit=$(git rev-parse --short=12 master^) &&
	commit2=$(git rev-parse --short=12 master~2) &&
	commit3=$(git rev-parse --short=12 master~3) &&

	# The first detach operation is more chatty than the following ones.
	cat >1st_detach <<-EOF &&
	Note: checking out 'HEAD^'.

	You are in 'detached HEAD' state. You can look around, make experimental
	changes and commit them, and you can discard any commits you make in this
	state without impacting any branches by performing another checkout.

	If you want to create a new branch to retain commits you create, you may
	do so (now or later) by using -b with the checkout command again. Example:

	  git checkout -b <new-branch-name>

	HEAD is now at \$commit... three
	EOF

	# The remaining ones just show info about previous and current HEADs.
	cat >2nd_detach <<-EOF &&
	Previous HEAD position was \$commit... three
	HEAD is now at \$commit2... two
	EOF

	cat >3rd_detach <<-EOF &&
	Previous HEAD position was \$commit2... two
	HEAD is now at \$commit3... one
	EOF

	reset &&
	check_not_detached &&

	# Various ways of asking for ellipses...
	# The user can just use any kind of quoting (including none).

	GIT_PRINT_SHA1_ELLIPSIS=yes git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 1st_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS=Yes git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 2nd_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS=YES git -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_i18ncmp 3rd_detach actual &&

	true
"

test_done
