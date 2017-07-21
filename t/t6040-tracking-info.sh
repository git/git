#!/bin/sh

test_description='remote tracking stats'

. ./test-lib.sh

advance () {
	echo "$1" >"$1" &&
	git add "$1" &&
	test_tick &&
	git commit -m "$1"
}

test_expect_success setup '
	advance a &&
	advance b &&
	advance c &&
	git clone . test &&
	(
		cd test &&
		git checkout -b b1 origin &&
		git reset --hard HEAD^ &&
		advance d &&
		git checkout -b b2 origin &&
		git reset --hard b1 &&
		git checkout -b b3 origin &&
		git reset --hard HEAD^ &&
		git checkout -b b4 origin &&
		advance e &&
		advance f &&
		git checkout -b brokenbase origin &&
		git checkout -b b5 --track brokenbase &&
		advance g &&
		git branch -d brokenbase &&
		git checkout -b b6 origin
	) &&
	git checkout -b follower --track master &&
	advance h
'

script='s/^..\(b.\) *[0-9a-f]* \(.*\)$/\1 \2/p'
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
		git branch -v
	) |
	sed -n -e "$script" >actual &&
	test_i18ncmp expect actual
'

cat >expect <<\EOF
b1 [origin/master: ahead 1, behind 1] d
b2 [origin/master: ahead 1, behind 1] d
b3 [origin/master: behind 1] b
b4 [origin/master: ahead 2] f
b5 [brokenbase: gone] g
b6 [origin/master] c
EOF

test_expect_success 'branch -vv' '
	(
		cd test &&
		git branch -vv
	) |
	sed -n -e "$script" >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'checkout (diverged from upstream)' '
	(
		cd test && git checkout b1
	) >actual &&
	test_i18ngrep "have 1 and 1 different" actual
'

test_expect_success 'checkout with local tracked branch' '
	git checkout master &&
	git checkout follower >actual &&
	test_i18ngrep "is ahead of" actual
'

test_expect_success 'checkout (upstream is gone)' '
	(
		cd test &&
		git checkout b5
	) >actual &&
	test_i18ngrep "is based on .*, but the upstream is gone." actual
'

test_expect_success 'checkout (up-to-date with upstream)' '
	(
		cd test && git checkout b6
	) >actual &&
	test_i18ngrep "Your branch is up-to-date with .origin/master" actual
'

test_expect_success 'status (diverged from upstream)' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		# reports nothing to commit
		test_must_fail git commit --dry-run
	) >actual &&
	test_i18ngrep "have 1 and 1 different" actual
'

test_expect_success 'status (upstream is gone)' '
	(
		cd test &&
		git checkout b5 >/dev/null &&
		# reports nothing to commit
		test_must_fail git commit --dry-run
	) >actual &&
	test_i18ngrep "is based on .*, but the upstream is gone." actual
'

test_expect_success 'status (up-to-date with upstream)' '
	(
		cd test &&
		git checkout b6 >/dev/null &&
		# reports nothing to commit
		test_must_fail git commit --dry-run
	) >actual &&
	test_i18ngrep "Your branch is up-to-date with .origin/master" actual
'

cat >expect <<\EOF
## b1...origin/master [ahead 1, behind 1]
EOF

test_expect_success 'status -s -b (diverged from upstream)' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		git status -s -b | head -1
	) >actual &&
	test_i18ncmp expect actual
'

cat >expect <<\EOF
## b5...brokenbase [gone]
EOF

test_expect_success 'status -s -b (upstream is gone)' '
	(
		cd test &&
		git checkout b5 >/dev/null &&
		git status -s -b | head -1
	) >actual &&
	test_i18ncmp expect actual
'

cat >expect <<\EOF
## b6...origin/master
EOF

test_expect_success 'status -s -b (up-to-date with upstream)' '
	(
		cd test &&
		git checkout b6 >/dev/null &&
		git status -s -b | head -1
	) >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'fail to track lightweight tags' '
	git checkout master &&
	git tag light &&
	test_must_fail git branch --track lighttrack light >actual &&
	test_i18ngrep ! "set up to track" actual &&
	test_must_fail git checkout lighttrack
'

test_expect_success 'fail to track annotated tags' '
	git checkout master &&
	git tag -m heavy heavy &&
	test_must_fail git branch --track heavytrack heavy >actual &&
	test_i18ngrep ! "set up to track" actual &&
	test_must_fail git checkout heavytrack
'

test_expect_success 'setup tracking with branch --set-upstream on existing branch' '
	git branch from-master master &&
	test_must_fail git config branch.from-master.merge > actual &&
	git branch --set-upstream from-master master &&
	git config branch.from-master.merge > actual &&
	grep -q "^refs/heads/master$" actual
'

test_expect_success '--set-upstream does not change branch' '
	git branch from-master2 master &&
	test_must_fail git config branch.from-master2.merge > actual &&
	git rev-list from-master2 &&
	git update-ref refs/heads/from-master2 from-master2^ &&
	git rev-parse from-master2 >expect2 &&
	git branch --set-upstream from-master2 master &&
	git config branch.from-master.merge > actual &&
	git rev-parse from-master2 >actual2 &&
	grep -q "^refs/heads/master$" actual &&
	cmp expect2 actual2
'

test_expect_success '--set-upstream @{-1}' '
	git checkout from-master &&
	git checkout from-master2 &&
	git config branch.from-master2.merge > expect2 &&
	git branch --set-upstream @{-1} follower &&
	git config branch.from-master.merge > actual &&
	git config branch.from-master2.merge > actual2 &&
	git branch --set-upstream from-master follower &&
	git config branch.from-master.merge > expect &&
	test_cmp expect2 actual2 &&
	test_cmp expect actual
'

test_done
