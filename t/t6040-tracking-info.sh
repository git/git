#!/bin/sh

test_description='remote tracking stats'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

advance () {
	echo "$1" >"$1" &&
	but add "$1" &&
	test_tick &&
	but cummit -m "$1"
}

test_expect_success setup '
	advance a &&
	advance b &&
	advance c &&
	but clone . test &&
	(
		cd test &&
		but checkout -b b1 origin &&
		but reset --hard HEAD^ &&
		advance d &&
		but checkout -b b2 origin &&
		but reset --hard b1 &&
		but checkout -b b3 origin &&
		but reset --hard HEAD^ &&
		but checkout -b b4 origin &&
		advance e &&
		advance f &&
		but checkout -b brokenbase origin &&
		but checkout -b b5 --track brokenbase &&
		advance g &&
		but branch -d brokenbase &&
		but checkout -b b6 origin
	) &&
	but checkout -b follower --track main &&
	advance h
'

t6040_script='s/^..\(b.\) *[0-9a-f]* \(.*\)$/\1 \2/p'
cat >expect <<\EOF
b1 [ahead 1, behind 1] d
b2 [ahead 1, behind 1] d
b3 [behind 1] b
b4 [ahead 2] f
b5 [gone] g
b6 c
EOF

test_expect_success 'branch -v' '
	(
		cd test &&
		but branch -v
	) |
	sed -n -e "$t6040_script" >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
b1 [origin/main: ahead 1, behind 1] d
b2 [origin/main: ahead 1, behind 1] d
b3 [origin/main: behind 1] b
b4 [origin/main: ahead 2] f
b5 [brokenbase: gone] g
b6 [origin/main] c
EOF

test_expect_success 'branch -vv' '
	(
		cd test &&
		but branch -vv
	) |
	sed -n -e "$t6040_script" >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout (diverged from upstream)' '
	(
		cd test && but checkout b1
	) >actual &&
	test_i18ngrep "have 1 and 1 different" actual
'

test_expect_success 'checkout with local tracked branch' '
	but checkout main &&
	but checkout follower >actual &&
	test_i18ngrep "is ahead of" actual
'

test_expect_success 'checkout (upstream is gone)' '
	(
		cd test &&
		but checkout b5
	) >actual &&
	test_i18ngrep "is based on .*, but the upstream is gone." actual
'

test_expect_success 'checkout (up-to-date with upstream)' '
	(
		cd test && but checkout b6
	) >actual &&
	test_i18ngrep "Your branch is up to date with .origin/main" actual
'

test_expect_success 'status (diverged from upstream)' '
	(
		cd test &&
		but checkout b1 >/dev/null &&
		# reports nothing to cummit
		test_must_fail but cummit --dry-run
	) >actual &&
	test_i18ngrep "have 1 and 1 different" actual
'

test_expect_success 'status (upstream is gone)' '
	(
		cd test &&
		but checkout b5 >/dev/null &&
		# reports nothing to cummit
		test_must_fail but cummit --dry-run
	) >actual &&
	test_i18ngrep "is based on .*, but the upstream is gone." actual
'

test_expect_success 'status (up-to-date with upstream)' '
	(
		cd test &&
		but checkout b6 >/dev/null &&
		# reports nothing to cummit
		test_must_fail but cummit --dry-run
	) >actual &&
	test_i18ngrep "Your branch is up to date with .origin/main" actual
'

cat >expect <<\EOF
## b1...origin/main [ahead 1, behind 1]
EOF

test_expect_success 'status -s -b (diverged from upstream)' '
	(
		cd test &&
		but checkout b1 >/dev/null &&
		but status -s -b | head -1
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
## b1...origin/main [different]
EOF

test_expect_success 'status -s -b --no-ahead-behind (diverged from upstream)' '
	(
		cd test &&
		but checkout b1 >/dev/null &&
		but status -s -b --no-ahead-behind | head -1
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
## b1...origin/main [different]
EOF

test_expect_success 'status.aheadbehind=false status -s -b (diverged from upstream)' '
	(
		cd test &&
		but checkout b1 >/dev/null &&
		but -c status.aheadbehind=false status -s -b | head -1
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
On branch b1
Your branch and 'origin/main' have diverged,
and have 1 and 1 different cummits each, respectively.
EOF

test_expect_success 'status --long --branch' '
	(
		cd test &&
		but checkout b1 >/dev/null &&
		but status --long -b | head -3
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'status --long --branch' '
	(
		cd test &&
		but checkout b1 >/dev/null &&
		but -c status.aheadbehind=true status --long -b | head -3
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
On branch b1
Your branch and 'origin/main' refer to different cummits.
EOF

test_expect_success 'status --long --branch --no-ahead-behind' '
	(
		cd test &&
		but checkout b1 >/dev/null &&
		but status --long -b --no-ahead-behind | head -2
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'status.aheadbehind=false status --long --branch' '
	(
		cd test &&
		but checkout b1 >/dev/null &&
		but -c status.aheadbehind=false status --long -b | head -2
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
## b5...brokenbase [gone]
EOF

test_expect_success 'status -s -b (upstream is gone)' '
	(
		cd test &&
		but checkout b5 >/dev/null &&
		but status -s -b | head -1
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
## b6...origin/main
EOF

test_expect_success 'status -s -b (up-to-date with upstream)' '
	(
		cd test &&
		but checkout b6 >/dev/null &&
		but status -s -b | head -1
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'fail to track lightweight tags' '
	but checkout main &&
	but tag light &&
	test_must_fail but branch --track lighttrack light >actual &&
	test_i18ngrep ! "set up to track" actual &&
	test_must_fail but checkout lighttrack
'

test_expect_success 'fail to track annotated tags' '
	but checkout main &&
	but tag -m heavy heavy &&
	test_must_fail but branch --track heavytrack heavy >actual &&
	test_i18ngrep ! "set up to track" actual &&
	test_must_fail but checkout heavytrack
'

test_expect_success '--set-upstream-to does not change branch' '
	but branch from-main main &&
	but branch --set-upstream-to main from-main &&
	but branch from-topic_2 main &&
	test_must_fail but config branch.from-topic_2.merge > actual &&
	but rev-list from-topic_2 &&
	but update-ref refs/heads/from-topic_2 from-topic_2^ &&
	but rev-parse from-topic_2 >expect2 &&
	but branch --set-upstream-to main from-topic_2 &&
	but config branch.from-main.merge > actual &&
	but rev-parse from-topic_2 >actual2 &&
	grep -q "^refs/heads/main$" actual &&
	cmp expect2 actual2
'

test_expect_success '--set-upstream-to @{-1}' '
	but checkout follower &&
	but checkout from-topic_2 &&
	but config branch.from-topic_2.merge > expect2 &&
	but branch --set-upstream-to @{-1} from-main &&
	but config branch.from-main.merge > actual &&
	but config branch.from-topic_2.merge > actual2 &&
	but branch --set-upstream-to follower from-main &&
	but config branch.from-main.merge > expect &&
	test_cmp expect2 actual2 &&
	test_cmp expect actual
'

test_done
