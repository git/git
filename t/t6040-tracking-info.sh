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
	for i in a b c;
	do
		advance $i || break
	done &&
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
		advance f
	) &&
	git checkout -b follower --track master &&
	advance g
'

script='s/^..\(b.\)[	 0-9a-f]*\[\([^]]*\)\].*/\1 \2/p'
cat >expect <<\EOF
b1 ahead 1, behind 1
b2 ahead 1, behind 1
b3 behind 1
b4 ahead 2
EOF

test_expect_success 'branch -v' '
	(
		cd test &&
		git branch -v
	) |
	sed -n -e "$script" >actual &&
	test_i18ncmp expect actual
'

test_expect_success 'checkout' '
	(
		cd test && git checkout b1
	) >actual &&
	grep "have 1 and 1 different" actual
'

test_expect_success 'checkout with local tracked branch' '
	git checkout master &&
	git checkout follower >actual &&
	grep "is ahead of" actual
'

test_expect_success 'status' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		# reports nothing to commit
		test_must_fail git commit --dry-run
	) >actual &&
	grep "have 1 and 1 different" actual
'

test_expect_success 'fail to track lightweight tags' '
	git checkout master &&
	git tag light &&
	test_must_fail git branch --track lighttrack light >actual &&
	test_must_fail grep "set up to track" actual &&
	test_must_fail git checkout lighttrack
'

test_expect_success 'fail to track annotated tags' '
	git checkout master &&
	git tag -m heavy heavy &&
	test_must_fail git branch --track heavytrack heavy >actual &&
	test_must_fail grep "set up to track" actual &&
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
test_done
