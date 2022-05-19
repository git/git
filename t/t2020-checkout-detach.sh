#!/bin/sh

test_description='checkout into detached HEAD state'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_detached () {
	test_must_fail but symbolic-ref -q HEAD >/dev/null
}

check_not_detached () {
	but symbolic-ref -q HEAD >/dev/null
}

PREV_HEAD_DESC='Previous HEAD position was'
check_orphan_warning() {
	test_i18ngrep "you are leaving $2 behind" "$1" &&
	test_i18ngrep ! "$PREV_HEAD_DESC" "$1"
}
check_no_orphan_warning() {
	test_i18ngrep ! "you are leaving .* cummit.*behind" "$1" &&
	test_i18ngrep "$PREV_HEAD_DESC" "$1"
}

reset () {
	but checkout main &&
	check_not_detached
}

test_expect_success 'setup' '
	test_cummit one &&
	test_cummit two &&
	test_cummit three && but tag -d three &&
	test_cummit four && but tag -d four &&
	but branch branch &&
	but tag tag
'

test_expect_success 'checkout branch does not detach' '
	reset &&
	but checkout branch &&
	check_not_detached
'

test_expect_success 'checkout tag detaches' '
	reset &&
	but checkout tag &&
	check_detached
'

test_expect_success 'checkout branch by full name detaches' '
	reset &&
	but checkout refs/heads/branch &&
	check_detached
'

test_expect_success 'checkout non-ref detaches' '
	reset &&
	but checkout branch^ &&
	check_detached
'

test_expect_success 'checkout ref^0 detaches' '
	reset &&
	but checkout branch^0 &&
	check_detached
'

test_expect_success 'checkout --detach detaches' '
	reset &&
	but checkout --detach branch &&
	check_detached
'

test_expect_success 'checkout --detach without branch name' '
	reset &&
	but checkout --detach &&
	check_detached
'

test_expect_success 'checkout --detach errors out for non-cummit' '
	reset &&
	test_must_fail but checkout --detach one^{tree} &&
	check_not_detached
'

test_expect_success 'checkout --detach errors out for extra argument' '
	reset &&
	but checkout main &&
	test_must_fail but checkout --detach tag one.t &&
	check_not_detached
'

test_expect_success 'checkout --detached and -b are incompatible' '
	reset &&
	test_must_fail but checkout --detach -b newbranch tag &&
	check_not_detached
'

test_expect_success 'checkout --detach moves HEAD' '
	reset &&
	but checkout one &&
	but checkout --detach two &&
	but diff --exit-code HEAD &&
	but diff --exit-code two
'

test_expect_success 'checkout warns on orphan cummits' '
	reset &&
	but checkout --detach two &&
	echo content >orphan &&
	but add orphan &&
	but cummit -a -m orphan1 &&
	echo new content >orphan &&
	but cummit -a -m orphan2 &&
	orphan2=$(but rev-parse HEAD) &&
	but checkout main 2>stderr
'

test_expect_success 'checkout warns on orphan cummits: output' '
	check_orphan_warning stderr "2 cummits"
'

test_expect_success 'checkout warns orphaning 1 of 2 cummits' '
	but checkout "$orphan2" &&
	but checkout HEAD^ 2>stderr
'

test_expect_success 'checkout warns orphaning 1 of 2 cummits: output' '
	check_orphan_warning stderr "1 cummit"
'

test_expect_success 'checkout does not warn leaving ref tip' '
	reset &&
	but checkout --detach two &&
	but checkout main 2>stderr
'

test_expect_success 'checkout does not warn leaving ref tip' '
	check_no_orphan_warning stderr
'

test_expect_success 'checkout does not warn leaving reachable cummit' '
	reset &&
	but checkout --detach HEAD^ &&
	but checkout main 2>stderr
'

test_expect_success 'checkout does not warn leaving reachable cummit' '
	check_no_orphan_warning stderr
'

cat >expect <<'EOF'
Your branch is behind 'main' by 1 cummit, and can be fast-forwarded.
  (use "but pull" to update your local branch)
EOF
test_expect_success 'tracking count is accurate after orphan check' '
	reset &&
	but branch child main^ &&
	but config branch.child.remote . &&
	but config branch.child.merge refs/heads/main &&
	but checkout child^ &&
	but checkout child >stdout &&
	test_cmp expect stdout
'

test_expect_success 'no advice given for explicit detached head state' '
	# baseline
	test_config advice.detachedHead true &&
	but checkout child && but checkout HEAD^0 >expect.advice 2>&1 &&
	test_config advice.detachedHead false &&
	but checkout child && but checkout HEAD^0 >expect.no-advice 2>&1 &&
	test_unconfig advice.detachedHead &&
	# without configuration, the advice.* variables default to true
	but checkout child && but checkout HEAD^0 >actual 2>&1 &&
	test_cmp expect.advice actual &&

	# with explicit --detach
	# no configuration
	test_unconfig advice.detachedHead &&
	but checkout child && but checkout --detach HEAD^0 >actual 2>&1 &&
	test_cmp expect.no-advice actual &&

	# explicitly decline advice
	test_config advice.detachedHead false &&
	but checkout child && but checkout --detach HEAD^0 >actual 2>&1 &&
	test_cmp expect.no-advice actual
'

# Detached HEAD tests for GIT_PRINT_SHA1_ELLIPSIS (new format)
test_expect_success 'describe_detached_head prints no SHA-1 ellipsis when not asked to' "

	cummit=$(but rev-parse --short=12 main^) &&
	cummit2=$(but rev-parse --short=12 main~2) &&
	cummit3=$(but rev-parse --short=12 main~3) &&

	# The first detach operation is more chatty than the following ones.
	cat >1st_detach <<-EOF &&
	Note: switching to 'HEAD^'.

	You are in 'detached HEAD' state. You can look around, make experimental
	changes and cummit them, and you can discard any cummits you make in this
	state without impacting any branches by switching back to a branch.

	If you want to create a new branch to retain cummits you create, you may
	do so (now or later) by using -c with the switch command. Example:

	  but switch -c <new-branch-name>

	Or undo this operation with:

	  but switch -

	Turn off this advice by setting config variable advice.detachedHead to false

	HEAD is now at \$cummit three
	EOF

	# The remaining ones just show info about previous and current HEADs.
	cat >2nd_detach <<-EOF &&
	Previous HEAD position was \$cummit three
	HEAD is now at \$cummit2 two
	EOF

	cat >3rd_detach <<-EOF &&
	Previous HEAD position was \$cummit2 two
	HEAD is now at \$cummit3 one
	EOF

	reset &&
	check_not_detached &&

	# Various ways of *not* asking for ellipses

	sane_unset GIT_PRINT_SHA1_ELLIPSIS &&
	but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 1st_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS="no" but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 2nd_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS= but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 3rd_detach actual &&

	sane_unset GIT_PRINT_SHA1_ELLIPSIS &&

	# We only have four cummits, but we can re-use them
	reset &&
	check_not_detached &&

	# Make no mention of the env var at all
	but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 1st_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS='nope' &&
	but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 2nd_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS=nein &&
	but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 3rd_detach actual &&

	true
"

# Detached HEAD tests for GIT_PRINT_SHA1_ELLIPSIS (old format)
test_expect_success 'describe_detached_head does print SHA-1 ellipsis when asked to' "

	cummit=$(but rev-parse --short=12 main^) &&
	cummit2=$(but rev-parse --short=12 main~2) &&
	cummit3=$(but rev-parse --short=12 main~3) &&

	# The first detach operation is more chatty than the following ones.
	cat >1st_detach <<-EOF &&
	Note: switching to 'HEAD^'.

	You are in 'detached HEAD' state. You can look around, make experimental
	changes and cummit them, and you can discard any cummits you make in this
	state without impacting any branches by switching back to a branch.

	If you want to create a new branch to retain cummits you create, you may
	do so (now or later) by using -c with the switch command. Example:

	  but switch -c <new-branch-name>

	Or undo this operation with:

	  but switch -

	Turn off this advice by setting config variable advice.detachedHead to false

	HEAD is now at \$cummit... three
	EOF

	# The remaining ones just show info about previous and current HEADs.
	cat >2nd_detach <<-EOF &&
	Previous HEAD position was \$cummit... three
	HEAD is now at \$cummit2... two
	EOF

	cat >3rd_detach <<-EOF &&
	Previous HEAD position was \$cummit2... two
	HEAD is now at \$cummit3... one
	EOF

	reset &&
	check_not_detached &&

	# Various ways of asking for ellipses...
	# The user can just use any kind of quoting (including none).

	GIT_PRINT_SHA1_ELLIPSIS=yes but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 1st_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS=Yes but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 2nd_detach actual &&

	GIT_PRINT_SHA1_ELLIPSIS=YES but -c 'core.abbrev=12' checkout HEAD^ >actual 2>&1 &&
	check_detached &&
	test_cmp 3rd_detach actual &&

	true
"

test_done
