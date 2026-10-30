#!/bin/sh

test_description='various tests of reflog walk (log -g) behavior'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'set up some reflog entries' '
	test_commit one &&
	test_commit two &&
	git checkout -b side HEAD^ &&
	test_commit three &&
	git merge --no-commit main &&
	echo evil-merge-content >>one.t &&
	test_tick &&
	git commit --no-edit -a
'

do_walk () {
	git log -g --format="%gd %gs" "$@"
}

test_expect_success 'set up expected reflog' '
	cat >expect.all <<-EOF
	HEAD@{0} commit (merge): Merge branch ${SQ}main${SQ} into side
	HEAD@{1} commit: three
	HEAD@{2} checkout: moving from main to side
	HEAD@{3} commit: two
	HEAD@{4} commit (initial): one
	EOF
'

test_expect_success 'reflog walk shows expected logs' '
	do_walk >actual &&
	test_cmp expect.all actual
'

test_expect_success 'reflog can limit with --no-merges' '
	grep -v merge expect.all >expect &&
	do_walk --no-merges >actual &&
	test_cmp expect actual
'

test_expect_success 'reflog can limit with pathspecs' '
	grep two expect.all >expect &&
	do_walk -- two.t >actual &&
	test_cmp expect actual
'

test_expect_success 'pathspec limiting handles merges' '
	# we pick up:
	#   - the initial commit of one
	#   - the checkout back to commit one
	#   - the evil merge which touched one
	sed -n "1p;3p;5p" expect.all >expect &&
	do_walk -- one.t >actual &&
	test_cmp expect actual
'

test_expect_success '--parents shows true parents' '
	# convert newlines to spaces
	echo $(git rev-parse HEAD HEAD^1 HEAD^2) >expect &&
	git rev-list -g --parents -1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'walking multiple reflogs shows all' '
	# We expect to see all entries for all reflogs, but interleaved by
	# date, with order on the command line breaking ties. We
	# can use "sort" on the separate lists to generate this,
	# but note two tricks:
	#
	#   1. We use "{" as the delimiter, which lets us skip to the reflog
	#      date specifier as our second field, and then our "-n" numeric
	#      sort ignores the bits after the timestamp.
	#
	#   2. POSIX leaves undefined whether this is a stable sort or not. So
	#      we use "-k 1" to ensure that we see HEAD before main before
	#      side when breaking ties.
	{
		do_walk --date=unix HEAD &&
		do_walk --date=unix side &&
		do_walk --date=unix main
	} >expect.raw &&
	sort -t "{" -k 2nr -k 1 <expect.raw >expect &&
	do_walk --date=unix HEAD main side >actual &&
	test_cmp expect actual
'

test_expect_success 'date-limiting does not interfere with other logs' '
	do_walk HEAD@{1979-01-01} HEAD >actual &&
	test_cmp expect.all actual
'

test_expect_success 'min/max age uses entry date to limit' '
	# Flip between commits one and two so each ref update actually
	# does something (and does not get optimized out). We know
	# that the timestamps of those commits will be before our "min".

	git update-ref -m before refs/heads/minmax one &&

	test_tick &&
	min=$test_tick &&
	git update-ref -m min refs/heads/minmax two &&

	test_tick &&
	max=$test_tick &&
	git update-ref -m max refs/heads/minmax one &&

	test_tick &&
	git update-ref -m after refs/heads/minmax two &&

	cat >expect <<-\EOF &&
	max
	min
	EOF
	git log -g --since=$min --until=$max --format=%gs minmax >actual &&
	test_cmp expect actual
'

# Create a situation where the reflog and ref database disagree about the latest
# state of HEAD.
test_expect_success 'walk prefers reflog to ref tip' '
	test_commit A &&
	test_commit B &&
	git reflog delete HEAD@{0} &&
	head=$(git rev-parse HEAD) &&
	git rev-parse A >expect &&
	git log -g --format=%H -1 >actual &&
	test_cmp expect actual
'

test_expect_success '%gb names the branch HEAD was on' '
	git init branches &&
	(
		cd branches &&
		test_commit base &&
		git switch -c topic &&
		test_commit A &&
		test_commit B &&
		git switch main &&
		test_commit upstream &&
		git switch topic &&
		git rebase main &&

		cat >expect <<-\EOF &&
		(topic) rebase (finish): returning to refs/heads/topic
		(topic) rebase (pick): B
		(topic) rebase (pick): A
		(topic) rebase (start): checkout main
		(topic) checkout: moving from main to topic
		(main) commit: upstream
		(main) checkout: moving from topic to main
		(topic) commit: B
		(topic) commit: A
		(topic) checkout: moving from main to topic
		(main) commit (initial): base
		EOF
		git log -g --format="(%gb) %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb names the ref of other reflogs' '
	(
		cd branches &&
		echo main >expect &&
		git log -g -1 --format=%gb main >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb follows "git rebase <upstream> <branch>"' '
	(
		cd branches &&
		git switch main &&
		test_commit main-change &&
		git rebase main topic &&
		git switch main &&
		git rebase main topic &&

		cat >expect <<-\EOF &&
		(topic) rebase: checkout topic
		(main) checkout: moving from topic to main
		(topic) rebase (finish): returning to refs/heads/topic
		(topic) rebase (pick): B
		(topic) rebase (pick): A
		(topic) rebase (start): checkout main
		(main) commit: main-change
		(main) checkout: moving from topic to main
		EOF
		git log -g -8 --format="(%gb) %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb names a detached HEAD by the commit it was left at' '
	(
		cd branches &&
		git switch --detach topic~1 &&
		test_commit detached &&
		git rebase topic &&
		rebased=$(git rev-parse HEAD) &&
		git switch topic &&

		left_before=$(git rev-parse --short detached) &&
		left_after=$(git rev-parse --short $rebased) &&
		cat >expect <<-EOF &&
		(topic) checkout: moving from $rebased to topic
		(detached at $left_after) rebase (pick): detached
		(detached at $left_after) rebase (start): checkout topic
		(detached at $left_before) commit: detached
		(detached at $left_before) checkout: moving from topic to topic~1
		EOF
		git log -g -5 --format="(%gb) %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb names the branch of a "git pull --rebase" in a fresh clone' '
	git clone branches downstream &&
	(
		cd branches &&
		test_commit upstream-change
	) &&
	(
		cd downstream &&
		test_commit local-change &&
		git pull --rebase &&

		echo "(topic)" >expect &&
		git log -g --format="(%gb)" >out &&
		sort -u out >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb follows a renamed branch' '
	(
		cd branches &&
		test_commit before-rename &&
		git branch -m topic renamed &&
		test_commit after-rename &&

		cat >expect <<-\EOF &&
		(renamed) commit: after-rename
		(renamed) Branch: renamed refs/heads/topic to refs/heads/renamed
		(topic) commit: before-rename
		EOF
		git log -g -3 --format="(%gb) %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'rev-list -g complains when there are no reflogs' '
	test_must_fail git rev-list -g
'

test_done
