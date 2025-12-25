#!/bin/sh

test_description='remote tracking stats'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
		git symbolic-ref refs/remotes/origin/HEAD refs/remotes/origin/main &&
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
	git checkout -b follower --track main &&
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
		git branch -v
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
		git branch -vv
	) |
	sed -n -e "$t6040_script" >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout (diverged from upstream)' '
	(
		cd test && git checkout b1
	) >actual &&
	test_grep "have 1 and 1 different" actual
'

test_expect_success 'checkout with local tracked branch' '
	git checkout main &&
	git checkout follower >actual &&
	test_grep "is ahead of" actual
'

test_expect_success 'checkout (upstream is gone)' '
	(
		cd test &&
		git checkout b5
	) >actual &&
	test_grep "is based on .*, but the upstream is gone." actual
'

test_expect_success 'checkout (up-to-date with upstream)' '
	(
		cd test && git checkout b6
	) >actual &&
	test_grep "Your branch is up to date with .origin/main" actual
'

test_expect_success 'status (diverged from upstream)' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		# reports nothing to commit
		test_must_fail git commit --dry-run
	) >actual &&
	test_grep "have 1 and 1 different" actual
'

test_expect_success 'status (upstream is gone)' '
	(
		cd test &&
		git checkout b5 >/dev/null &&
		# reports nothing to commit
		test_must_fail git commit --dry-run
	) >actual &&
	test_grep "is based on .*, but the upstream is gone." actual
'

test_expect_success 'status (up-to-date with upstream)' '
	(
		cd test &&
		git checkout b6 >/dev/null &&
		# reports nothing to commit
		test_must_fail git commit --dry-run
	) >actual &&
	test_grep "Your branch is up to date with .origin/main" actual
'

cat >expect <<\EOF
## b1...origin/main [ahead 1, behind 1]
EOF

test_expect_success 'status -s -b (diverged from upstream)' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		git status -s -b | head -1
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
## b1...origin/main [different]
EOF

test_expect_success 'status -s -b --no-ahead-behind (diverged from upstream)' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		git status -s -b --no-ahead-behind | head -1
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
## b1...origin/main [different]
EOF

test_expect_success 'status.aheadbehind=false status -s -b (diverged from upstream)' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		git -c status.aheadbehind=false status -s -b | head -1
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
On branch b1
Your branch and 'origin/main' have diverged,
and have 1 and 1 different commits each, respectively.
EOF

test_expect_success 'status --long --branch' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		git status --long -b | head -3
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'status --long --branch' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		git -c status.aheadbehind=true status --long -b | head -3
	) >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
On branch b1
Your branch and 'origin/main' refer to different commits.
EOF

test_expect_success 'status --long --branch --no-ahead-behind' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		git status --long -b --no-ahead-behind | head -2
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'status.aheadbehind=false status --long --branch' '
	(
		cd test &&
		git checkout b1 >/dev/null &&
		git -c status.aheadbehind=false status --long -b | head -2
	) >actual &&
	test_cmp expect actual
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
	test_cmp expect actual
'

cat >expect <<\EOF
## b6...origin/main
EOF

test_expect_success 'status -s -b (up-to-date with upstream)' '
	(
		cd test &&
		git checkout b6 >/dev/null &&
		git status -s -b | head -1
	) >actual &&
	test_cmp expect actual
'

test_expect_success 'fail to track lightweight tags' '
	git checkout main &&
	git tag light &&
	test_must_fail git branch --track lighttrack light >actual &&
	test_grep ! "set up to track" actual &&
	test_must_fail git checkout lighttrack
'

test_expect_success 'fail to track annotated tags' '
	git checkout main &&
	git tag -m heavy heavy &&
	test_must_fail git branch --track heavytrack heavy >actual &&
	test_grep ! "set up to track" actual &&
	test_must_fail git checkout heavytrack
'

test_expect_success '--set-upstream-to does not change branch' '
	git branch from-main main &&
	git branch --set-upstream-to main from-main &&
	git branch from-topic_2 main &&
	test_must_fail git config branch.from-topic_2.merge > actual &&
	git rev-list from-topic_2 &&
	git update-ref refs/heads/from-topic_2 from-topic_2^ &&
	git rev-parse from-topic_2 >expect2 &&
	git branch --set-upstream-to main from-topic_2 &&
	git config branch.from-main.merge > actual &&
	git rev-parse from-topic_2 >actual2 &&
	grep -q "^refs/heads/main$" actual &&
	cmp expect2 actual2
'

test_expect_success '--set-upstream-to @{-1}' '
	git checkout follower &&
	git checkout from-topic_2 &&
	git config branch.from-topic_2.merge > expect2 &&
	git branch --set-upstream-to @{-1} from-main &&
	git config branch.from-main.merge > actual &&
	git config branch.from-topic_2.merge > actual2 &&
	git branch --set-upstream-to follower from-main &&
	git config branch.from-main.merge > expect &&
	test_cmp expect2 actual2 &&
	test_cmp expect actual
'

test_expect_success 'setup for ahead of non-main tracking branch' '
	(
		cd test &&
		git checkout -b feature origin/main &&
		advance feature1 &&
		git push origin feature &&
		git checkout -b work --track origin/feature &&
		advance work1 &&
		advance work2
	)
'

test_expect_success 'status shows ahead of both tracked branch and origin/main' '
	(
		cd test &&
		git checkout work >/dev/null &&
		git config status.goalBranch origin/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch work
Your branch is ahead of '\''origin/feature'\'' by 2 commits.
  (use "git push" to publish your local commits)

Ahead of '\''origin/main'\'' by 3 commits.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'checkout shows ahead of both tracked branch and origin/main' '
	(
		cd test &&
		git checkout main >/dev/null &&
		git config status.goalBranch origin/main &&
		git checkout work 2>&1
	) >actual &&
	cat >expect <<-\EOF &&
Switched to branch '\''work'\''
Your branch is ahead of '\''origin/feature'\'' by 2 commits.
  (use "git push" to publish your local commits)

Ahead of '\''origin/main'\'' by 3 commits.
EOF
	test_cmp expect actual
'

test_expect_success 'status tracking origin/main shows only main' '
	(
		cd test &&
		git checkout b4 >/dev/null &&
		git status --long -b
	) >actual &&
	test_grep "ahead of .origin/main. by 2 commits" actual &&
	test_grep ! "Ahead of" actual
'

test_expect_success 'setup for ahead of tracked but diverged from main' '
	(
		cd test &&
		git checkout origin/main &&
		git checkout -b oldfeature &&
		advance oldfeature1 &&
		git push origin oldfeature &&
		git checkout origin/main &&
		advance main_newer &&
		git push origin HEAD:main &&
		git checkout -b work2 --track origin/oldfeature &&
		advance work2_commit
	)
'

test_expect_success 'status shows ahead of tracked and diverged from origin/main' '
	(
		cd test &&
		git checkout work2 >/dev/null &&
		git config status.goalBranch origin/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch work2
Your branch is ahead of '\''origin/oldfeature'\'' by 1 commit.
  (use "git push" to publish your local commits)

Diverged from '\''origin/main'\'' by 3 commits.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'setup for diverged from tracked but behind main' '
	(
		cd test &&
		git fetch origin &&
		git checkout origin/main &&
		git checkout -b work2b &&
		git branch --set-upstream-to=origin/oldfeature &&
		git checkout origin/main &&
		advance main_extra &&
		git push origin HEAD:main
	)
'

test_expect_success 'status shows diverged from tracked and behind origin/main' '
	(
		cd test &&
		git checkout work2b >/dev/null &&
		git config status.goalBranch origin/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch work2b
Your branch and '\''origin/oldfeature'\'' have diverged,
and have 1 and 1 different commits each, respectively.
  (use "git pull" if you want to integrate the remote branch with yours)

Behind '\''origin/main'\'' by 1 commit.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'setup for behind tracked but ahead of main' '
	(
		cd test &&
		git fetch origin &&
		git checkout origin/main &&
		git checkout -b feature3 &&
		advance feature3_1 &&
		advance feature3_2 &&
		advance feature3_3 &&
		git push origin feature3 &&
		git checkout -b work3 --track origin/feature3 &&
		git reset --hard HEAD~2
	)
'

test_expect_success 'status shows behind tracked and ahead of origin/main' '
	(
		cd test &&
		git checkout work3 >/dev/null &&
		git config status.goalBranch origin/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch work3
Your branch is behind '\''origin/feature3'\'' by 2 commits, and can be fast-forwarded.
  (use "git pull" to update your local branch)

Ahead of '\''origin/main'\'' by 1 commit.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'setup upstream remote preference' '
	(
		cd test &&
		git remote add upstream ../. &&
		git fetch upstream &&
		git symbolic-ref refs/remotes/upstream/HEAD refs/remotes/upstream/main
	)
'

test_expect_success 'status prefers upstream remote over origin for comparison' '
	(
		cd test &&
		git checkout work >/dev/null &&
		git config status.goalBranch upstream/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch work
Your branch is ahead of '\''origin/feature'\'' by 2 commits.
  (use "git push" to publish your local commits)

Diverged from '\''upstream/main'\'' by 5 commits.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'setup for up to date with tracked but ahead of default' '
	(
		cd test &&
		git checkout origin/feature &&
		git checkout -b synced_feature --track origin/feature &&
		git checkout origin/main &&
		advance main_ahead &&
		git push origin HEAD:main
	)
'

test_expect_success 'status shows up to date with tracked but diverged from default' '
	(
		cd test &&
		git checkout synced_feature >/dev/null &&
		git config status.goalBranch upstream/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch synced_feature
Your branch is up to date with '\''origin/feature'\''.

Diverged from '\''upstream/main'\'' by 3 commits.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'setup for up to date with tracked but ahead of origin/main' '
	(
		cd test &&
		git remote remove upstream &&
		git checkout origin/feature &&
		git checkout -b synced_feature2 --track origin/feature &&
		git checkout origin/main &&
		advance main_ahead2 &&
		git push origin HEAD:main
	)
'

test_expect_success 'status shows up to date with tracked but diverged from origin/main' '
	(
		cd test &&
		git checkout synced_feature2 >/dev/null &&
		git config status.goalBranch origin/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch synced_feature2
Your branch is up to date with '\''origin/feature'\''.

Diverged from '\''origin/main'\'' by 5 commits.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'setup for up to date with tracked but purely ahead of origin/main' '
	(
		cd test &&
		git checkout origin/feature &&
		git checkout -b synced_feature3 --track origin/feature
	)
'

test_expect_success 'status shows up to date with tracked but shows default branch comparison' '
	(
		cd test &&
		git checkout synced_feature3 >/dev/null &&
		git config status.goalBranch origin/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch synced_feature3
Your branch is up to date with '\''origin/feature'\''.

Diverged from '\''origin/main'\'' by 5 commits.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'status with status.goalBranch unset shows no default comparison' '
	(
		cd test &&
		git checkout synced_feature3 >/dev/null &&
		git config --unset status.goalBranch 2>/dev/null || true &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch synced_feature3
Your branch is up to date with '\''origin/feature'\''.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'status with status.goalBranch set uses configured branch' '
	(
		cd test &&
		git checkout synced_feature3 >/dev/null &&
		git config status.goalBranch origin/main &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch synced_feature3
Your branch is up to date with '\''origin/feature'\''.

Diverged from '\''origin/main'\'' by 5 commits.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'status with status.goalBranch set to different remote/branch' '
	(
		cd test &&
		git checkout work >/dev/null &&
		git config status.goalBranch origin/feature &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch work
Your branch is ahead of '\''origin/feature'\'' by 2 commits.
  (use "git push" to publish your local commits)

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_expect_success 'status with status.goalBranch set to non-existent branch' '
	(
		cd test &&
		git checkout synced_feature3 >/dev/null &&
		git config status.goalBranch origin/nonexistent &&
		git status --long -b
	) >actual &&
	cat >expect <<-\EOF &&
On branch synced_feature3
Your branch is up to date with '\''origin/feature'\''.

nothing to commit, working tree clean
EOF
	test_cmp expect actual
'

test_done
