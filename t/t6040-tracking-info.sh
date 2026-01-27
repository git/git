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

test_expect_success 'status tracking origin/main shows only main' '
	(
		cd test &&
		git checkout b4 &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch b4
	Your branch is ahead of ${SQ}origin/main${SQ} by 2 commits.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status --no-ahead-behind tracking origin/main shows only main' '
	(
		cd test &&
		git checkout b4 &&
		git status --no-ahead-behind >../actual
	) &&
	cat >expect <<-EOF &&
	On branch b4
	Your branch and ${SQ}origin/main${SQ} refer to different commits.
	  (use "git status --ahead-behind" for details)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'setup for compareBranches tests' '
	(
		cd test &&
		git config push.default current &&
		git config status.compareBranches "@{upstream} @{push}"
	)
'

test_expect_success 'status.compareBranches from upstream has no duplicates' '
	(
		cd test &&
		git checkout main &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch main
	Your branch is up to date with ${SQ}origin/main${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches shows ahead of both upstream and push branch' '
	(
		cd test &&
		git checkout -b feature2 origin/main &&
		git push origin HEAD &&
		advance work &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature2
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/feature2${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'checkout with status.compareBranches shows both branches' '
	(
		cd test &&
		git checkout feature2 >../actual
	) &&
	cat >expect <<-EOF &&
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/feature2${SQ} by 1 commit.
	  (use "git push" to publish your local commits)
	EOF
	test_cmp expect actual
'

test_expect_success 'setup for ahead of tracked but diverged from main' '
	(
		cd test &&
		git checkout -b feature4 origin/main &&
		advance work1 &&
		git checkout origin/main &&
		advance work2 &&
		git push origin HEAD:main &&
		git checkout feature4 &&
		advance work3
	)
'

test_expect_success 'status.compareBranches shows diverged and ahead' '
	(
		cd test &&
		git checkout feature4 &&
		git branch --set-upstream-to origin/main &&
		git push origin HEAD &&
		advance work &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature4
	Your branch and ${SQ}origin/main${SQ} have diverged,
	and have 3 and 1 different commits each, respectively.
	  (use "git pull" if you want to integrate the remote branch with yours)

	Your branch is ahead of ${SQ}origin/feature4${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status --no-ahead-behind with status.compareBranches' '
	(
		cd test &&
		git checkout feature4 &&
		git status --no-ahead-behind >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature4
	Your branch and ${SQ}origin/main${SQ} refer to different commits.

	Your branch and ${SQ}origin/feature4${SQ} refer to different commits.
	  (use "git status --ahead-behind" for details)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'setup upstream remote' '
	(
		cd test &&
		git remote add upstream ../. &&
		git fetch upstream &&
		git config remote.pushDefault origin
	)
'

test_expect_success 'status.compareBranches with upstream and origin remotes' '
	(
		cd test &&
		git checkout -b feature5 upstream/main &&
		git push origin &&
		advance work &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature5
	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/feature5${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with upstream and origin remotes multiple compare branches' '
	(
		cd test &&
		git checkout -b feature6 upstream/main &&
		git push origin &&
		advance work &&
		git -c status.compareBranches="upstream/main origin/feature6 origin/feature5" status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature6
	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/feature6${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	Your branch is ahead of ${SQ}origin/feature5${SQ} by 1 commit.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with diverged push branch' '
	(
		cd test &&
		git checkout -b feature7 upstream/main &&
		advance work &&
		git push origin &&
		git reset --hard upstream/main &&
		advance work &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature7
	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	Your branch and ${SQ}origin/feature7${SQ} have diverged,
	and have 1 and 1 different commits each, respectively.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches shows up to date branches' '
	(
		cd test &&
		git checkout -b feature8 upstream/main &&
		git push origin &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature8
	Your branch is up to date with ${SQ}upstream/main${SQ}.

	Your branch is up to date with ${SQ}origin/feature8${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status --no-ahead-behind with status.compareBranches up to date' '
	(
		cd test &&
		git checkout feature8 &&
		git push origin &&
		git status --no-ahead-behind >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature8
	Your branch is up to date with ${SQ}upstream/main${SQ}.

	Your branch is up to date with ${SQ}origin/feature8${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'checkout with status.compareBranches shows up to date' '
	(
		cd test &&
		git checkout feature8 >../actual
	) &&
	cat >expect <<-EOF &&
	Your branch is up to date with ${SQ}upstream/main${SQ}.

	Your branch is up to date with ${SQ}origin/feature8${SQ}.
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with upstream behind and push up to date' '
	(
		cd test &&
		git checkout -b ahead upstream/main &&
		advance work &&
		git push upstream HEAD &&
		git checkout -b feature9 upstream/main &&
		git push origin &&
		git branch --set-upstream-to upstream/ahead &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature9
	Your branch is behind ${SQ}upstream/ahead${SQ} by 1 commit, and can be fast-forwarded.
	  (use "git pull" to update your local branch)

	Your branch is up to date with ${SQ}origin/feature9${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with remapped push refspec' '
	(
		cd test &&
		git checkout -b feature10 origin/main &&
		git config remote.origin.push refs/heads/feature10:refs/heads/remapped &&
		git push &&
		advance work &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature10
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/remapped${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with remapped push and upstream remote' '
	(
		cd test &&
		git checkout -b feature11 upstream/main &&
		git config remote.origin.push refs/heads/feature11:refs/heads/remapped &&
		git push origin &&
		advance work &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature11
	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/remapped${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'clean up after compareBranches tests' '
	(
		cd test &&
		git config --unset status.compareBranches
	)
'

test_done
