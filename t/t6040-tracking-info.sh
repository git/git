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
	test_grep -q "^refs/heads/main$" actual &&
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
	git -C test checkout b4 &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch b4
	Your branch is ahead of ${SQ}origin/main${SQ} by 2 commits.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status --no-ahead-behind tracking origin/main shows only main' '
	git -C test checkout b4 &&
	git -C test status --no-ahead-behind >actual &&
	cat >expect <<-EOF &&
	On branch b4
	Your branch and ${SQ}origin/main${SQ} refer to different commits.
	  (use "git status --ahead-behind" for details)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches deduplicates when upstream and push are the same' '
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout main &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch main
	Your branch is up to date with ${SQ}origin/main${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with only upstream shows only upstream' '
	test_config -C test status.compareBranches "@{upstream}" &&
	git -C test checkout main &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch main
	Your branch is up to date with ${SQ}origin/main${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with only push shows only push' '
	test_config -C test push.default current &&
	test_config -C test status.compareBranches "@{push}" &&
	git -C test checkout -b feature2 origin/main &&
	git -C test push origin HEAD &&
	(cd test && advance work) &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature2
	Your branch is ahead of ${SQ}origin/feature2${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches shows ahead of both upstream and push branch' '
	test_config -C test push.default current &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b feature3 origin/main &&
	git -C test push origin HEAD &&
	(cd test && advance work) &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature3
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/feature3${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'checkout with status.compareBranches shows both branches' '
	test_config -C test push.default current &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout feature3 >actual &&
	cat >expect <<-EOF &&
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/feature3${SQ} by 1 commit.
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
	test_config -C test push.default current &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout feature4 &&
	git -C test branch --set-upstream-to origin/main &&
	git -C test push origin HEAD &&
	(cd test && advance work) &&
	git -C test status >actual &&
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
	test_config -C test push.default current &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout feature4 &&
	git -C test status --no-ahead-behind >actual &&
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
		git fetch upstream
	)
'

test_expect_success 'status.compareBranches with upstream and origin remotes' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b feature5 upstream/main &&
	git -C test push origin &&
	(cd test && advance work) &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature5
	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/feature5${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches supports ordered upstream/push entries' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{push} @{upstream}" &&
	git -C test checkout -b feature6 upstream/main &&
	git -C test push origin &&
	(cd test && advance work) &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature6
	Your branch is ahead of ${SQ}origin/feature6${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches deduplicates repeated specifiers' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{push} @{upstream} @{push}" &&
	git -C test checkout -b feature7 upstream/main &&
	git -C test push origin &&
	(cd test && advance work) &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature7
	Your branch is ahead of ${SQ}origin/feature7${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with diverged push branch' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b feature8 upstream/main &&
	(cd test && advance work81) &&
	git -C test push origin &&
	git -C test reset --hard upstream/main &&
	(cd test && advance work82) &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature8
	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	Your branch and ${SQ}origin/feature8${SQ} have diverged,
	and have 1 and 1 different commits each, respectively.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches shows up to date branches' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b feature9 upstream/main &&
	git -C test push origin &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature9
	Your branch is up to date with ${SQ}upstream/main${SQ}.

	Your branch is up to date with ${SQ}origin/feature9${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status --no-ahead-behind with status.compareBranches up to date' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout feature9 >actual &&
	git -C test push origin &&
	git -C test status --no-ahead-behind >actual &&
	cat >expect <<-EOF &&
	On branch feature9
	Your branch is up to date with ${SQ}upstream/main${SQ}.

	Your branch is up to date with ${SQ}origin/feature9${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'checkout with status.compareBranches shows up to date' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout feature9 >actual &&
	cat >expect <<-EOF &&
	Your branch is up to date with ${SQ}upstream/main${SQ}.

	Your branch is up to date with ${SQ}origin/feature9${SQ}.
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with upstream behind and push up to date' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b ahead upstream/main &&
	(cd test && advance work) &&
	git -C test push upstream HEAD &&
	git -C test checkout -b feature10 upstream/main &&
	git -C test push origin &&
	git -C test branch --set-upstream-to upstream/ahead &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature10
	Your branch is behind ${SQ}upstream/ahead${SQ} by 1 commit, and can be fast-forwarded.
	  (use "git pull" to update your local branch)

	Your branch is up to date with ${SQ}origin/feature10${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with remapped push refspec' '
	test_config -C test remote.origin.push refs/heads/feature11:refs/heads/remapped &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b feature11 origin/main &&
	git -C test push &&
	(cd test && advance work) &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature11
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/remapped${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with remapped push and upstream remote' '
	test_config -C test remote.pushDefault origin &&
	test_config -C test remote.origin.push refs/heads/feature12:refs/heads/remapped &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b feature12 upstream/main &&
	git -C test push origin &&
	(cd test && advance work) &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature12
	Your branch is ahead of ${SQ}upstream/main${SQ} by 1 commit.

	Your branch is ahead of ${SQ}origin/remapped${SQ} by 1 commit.
	  (use "git push" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches behind both upstream and push' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b feature13 upstream/main &&
	(cd test && advance work13) &&
	git -C test push origin &&
	git -C test branch --set-upstream-to upstream/ahead &&
	git -C test reset --hard HEAD^ &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature13
	Your branch is behind ${SQ}upstream/ahead${SQ} by 1 commit, and can be fast-forwarded.
	  (use "git pull" to update your local branch)

	Your branch is behind ${SQ}origin/feature13${SQ} by 1 commit, and can be fast-forwarded.
	  (use "git pull origin feature13" to update your local branch)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with remapped push and behind push branch' '
	test_config -C test remote.pushDefault origin &&
	test_config -C test remote.origin.push refs/heads/feature14:refs/heads/remapped14 &&
	test_config -C test status.compareBranches "@{push}" &&
	git -C test checkout -b feature14 upstream/main &&
	(cd test && advance work14) &&
	git -C test push &&
	git -C test reset --hard HEAD^ &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature14
	Your branch is behind ${SQ}origin/remapped14${SQ} by 1 commit, and can be fast-forwarded.
	  (use "git pull origin remapped14" to update your local branch)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches with behind push branch and no upstream' '
	test_config -C test push.default current &&
	test_config -C test remote.pushDefault origin &&
	test_config -C test status.compareBranches "@{push}" &&
	git -C test checkout --no-track -b feature15 upstream/main &&
	(cd test && advance work15) &&
	git -C test push origin &&
	git -C test reset --hard HEAD^ &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature15
	Your branch is behind ${SQ}origin/feature15${SQ} by 1 commit, and can be fast-forwarded.
	  (use "git pull origin feature15" to update your local branch)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches behind upstream-equals-push suggests plain pull' '
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	git -C test checkout -b feature16 origin/main &&
	(cd test && advance work16) &&
	git -C test push origin HEAD:main &&
	git -C test reset --hard HEAD^ &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature16
	Your branch is behind ${SQ}origin/main${SQ} by 1 commit, and can be fast-forwarded.
	  (use "git pull" to update your local branch)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches suppresses advice when push tracking ref is unconventional' '
	test_config -C test push.default current &&
	test_config -C test remote.imported.url ../. &&
	test_config -C test remote.imported.fetch "+refs/heads/*:refs/imported/imported/*" &&
	test_config -C test branch.feature17.pushRemote imported &&
	test_config -C test status.compareBranches "@{push}" &&
	git -C test fetch imported &&
	git -C test checkout --no-track -b feature17 refs/imported/imported/main &&
	(cd test && advance work17) &&
	git -C test push imported HEAD:feature17 &&
	git -C test fetch imported &&
	git -C test reset --hard HEAD^ &&
	git -C test status >actual &&
	cat >expect <<-EOF &&
	On branch feature17
	Your branch is behind ${SQ}imported/imported/feature17${SQ} by 1 commit, and can be fast-forwarded.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches counts push divergence outside upstream' '
	test_config -C test push.default current &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	(
		cd test &&
		git checkout -b feature18 origin/main &&
		advance work18 &&
		git push
	) &&
	git checkout main &&
	advance main18a &&
	advance main18b &&
	git checkout - &&
	(
		cd test &&
		echo amended >work18 &&
		git commit -a --amend --no-edit &&
		git pull --rebase &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature18
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch and ${SQ}origin/feature18${SQ} have diverged,
	and have 3 and 1 different commits each (1 and 1 not in ${SQ}origin/main${SQ}).

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'status.compareBranches after a clean rebase of the push branch' '
	test_config -C test push.default current &&
	test_config -C test status.compareBranches "@{upstream} @{push}" &&
	(
		cd test &&
		git checkout -b feature19 origin/main &&
		advance work19 &&
		git push
	) &&
	git checkout main &&
	advance main19a &&
	advance main19b &&
	git checkout - &&
	(
		cd test &&
		git pull --rebase &&
		git status >../actual
	) &&
	cat >expect <<-EOF &&
	On branch feature19
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch and ${SQ}origin/feature19${SQ} have diverged,
	and have 3 and 1 different commits each (rebased cleanly on ${SQ}origin/main${SQ}).
	  (use "git push --force-with-lease" to publish your local commits)

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual &&
	(
		cd test &&
		test_must_fail git push 2>../push.err &&
		git push --force-with-lease origin feature19 &&
		git status >../actual
	) &&
	url=$(git -C test config remote.origin.url) &&
	cat >expect <<-EOF &&
	To $url
	 ! [rejected]        feature19 -> feature19 (non-fast-forward)
	error: failed to push some refs to ${SQ}$url${SQ}
	hint: Updates were rejected because ${SQ}origin/feature19${SQ} has diverged
	hint: from your current branch, which was rebased cleanly on ${SQ}origin/main${SQ}.
	hint: Use ${SQ}git push --force-with-lease origin feature19${SQ} to replace it.
	EOF
	test_cmp expect push.err &&
	cat >expect <<-EOF &&
	On branch feature19
	Your branch is ahead of ${SQ}origin/main${SQ} by 1 commit.

	Your branch is up to date with ${SQ}origin/feature19${SQ}.

	nothing to commit, working tree clean
	EOF
	test_cmp expect actual
'

test_expect_success 'push to a push branch someone else updated suggests pulling from it' '
	(
		cd test &&
		git checkout -b feature20 origin/main &&
		advance work20 &&
		git push origin feature20
	) &&
	git checkout feature20 &&
	advance other20 &&
	git checkout - &&
	(
		cd test &&
		advance mine20 &&
		git fetch &&
		test_must_fail git push origin feature20 2>../actual
	) &&
	url=$(git -C test config remote.origin.url) &&
	cat >expect <<-EOF &&
	To $url
	 ! [rejected]        feature20 -> feature20 (non-fast-forward)
	error: failed to push some refs to ${SQ}$url${SQ}
	hint: Updates were rejected because ${SQ}origin/feature20${SQ} has diverged
	hint: from your current branch. Use ${SQ}git pull origin feature20${SQ}
	hint: to integrate the remote changes.
	EOF
	test_cmp expect actual
'

test_expect_success 'push to the upstream branch' '
	(
		cd test &&
		git checkout -b feature21 origin/main &&
		advance work21 &&
		git push -u origin feature21
	) &&
	git checkout feature21 &&
	advance other21 &&
	git checkout - &&
	(
		cd test &&
		advance mine21 &&
		git fetch &&
		test_must_fail git push 2>../actual
	) &&
	url=$(git -C test config remote.origin.url) &&
	cat >expect <<-EOF &&
	To $url
	 ! [rejected]        feature21 -> feature21 (non-fast-forward)
	error: failed to push some refs to ${SQ}$url${SQ}
	hint: Updates were rejected because the tip of your current branch is behind
	hint: its remote counterpart. If you want to integrate the remote changes,
	hint: use ${SQ}git pull${SQ} before pushing again.
	hint: See the ${SQ}Note about fast-forwards${SQ} in ${SQ}git push --help${SQ} for details.
	EOF
	test_cmp expect actual &&
	(
		cd test &&
		git pull --rebase &&
		git push &&
		echo amended >mine21 &&
		git commit -a --amend --no-edit &&
		test_must_fail git push 2>../actual
	) &&
	cat >expect <<-EOF &&
	To $url
	 ! [rejected]        feature21 -> feature21 (non-fast-forward)
	error: failed to push some refs to ${SQ}$url${SQ}
	hint: Updates were rejected because ${SQ}origin/feature21${SQ} has diverged
	hint: from your current branch. Use ${SQ}git pull origin feature21${SQ}
	hint: to integrate the remote changes, or replace them with
	hint: ${SQ}git push --force-with-lease origin feature21${SQ}.
	EOF
	test_cmp expect actual
'

test_done
