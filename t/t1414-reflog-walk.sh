#!/bin/sh

test_description='various tests of reflog walk (log -g) behavior'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-rebase.sh

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

test_expect_success '--operations shows a rebase as one entry' '
	git init operations &&
	(
		cd operations &&
		test_commit base &&
		git switch -c topic &&
		test_commit A &&
		test_commit B &&
		test_commit C &&
		git switch main &&
		test_commit upstream &&
		git switch topic &&
		git rebase main &&

		cat >expect <<-\EOF &&
		HEAD@{0} rebase (finish): returning to refs/heads/topic
		HEAD@{6} commit: upstream
		HEAD@{8} commit: C
		HEAD@{9} commit: B
		HEAD@{10} commit: A
		HEAD@{12} commit (initial): base
		EOF
		git log -g --format="%gd %gs" HEAD --operations >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--operations shows a stopped rebase as one entry once it finishes' '
	set_fake_editor &&
	(
		cd operations &&
		FAKE_LINES="edit 1 2" git rebase -i HEAD~2 &&

		cat >expect <<-\EOF &&
		HEAD@{0} rebase: fast-forward
		HEAD@{1} rebase (start): checkout HEAD~2
		EOF
		git log -g --operations --format="%gd %gs" -2 >actual &&
		test_cmp expect actual &&

		git commit --amend -m "B amended" &&
		git rebase --continue &&

		cat >expect <<-\EOF &&
		HEAD@{0} rebase (finish): returning to refs/heads/topic
		HEAD@{5} rebase (finish): returning to refs/heads/topic
		EOF
		git log -g --operations --format="%gd %gs" -2 >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--operations shows an aborted rebase as one entry' '
	(
		cd operations &&
		FAKE_LINES="edit 1 2" git rebase -i HEAD~2 &&
		git rebase --abort &&

		cat >expect <<-\EOF &&
		HEAD@{0} rebase (abort): returning to refs/heads/topic
		HEAD@{3} rebase (finish): returning to refs/heads/topic
		EOF
		git log -g --operations --format="%gd %gs" -2 >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--operations leaves out the reset done by git stash' '
	(
		cd operations &&
		echo change >>C.t &&
		git stash &&

		echo "HEAD@{1} rebase (abort): returning to refs/heads/topic" >expect &&
		git log -g --operations --format="%gd %gs" -1 >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--operations shows a pull --rebase as one entry' '
	git clone operations downstream &&
	(
		cd operations &&
		test_commit upstream-change
	) &&
	(
		cd downstream &&
		test_commit local-change &&
		git pull --rebase &&

		cat >expect <<-\EOF &&
		HEAD@{0} pull --rebase (finish): returning to refs/heads/topic
		HEAD@{3} commit: local-change
		EOF
		git log -g --operations --format="%gd %gs" -2 >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--operations requires --walk-reflogs' '
	test_must_fail git log --operations 2>err &&
	test_grep "requires .--walk-reflogs." err
'

test_expect_success 'rev-list -g complains when there are no reflogs' '
	test_must_fail git rev-list -g
'

test_done
