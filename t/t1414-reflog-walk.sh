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

test_expect_success '%gb follows "git rebase <upstream> <branch>" with GIT_REFLOG_ACTION' '
	(
		cd branches &&
		git switch main &&
		GIT_REFLOG_ACTION="sync switch" git rebase main topic &&

		cat >expect <<-\EOF &&
		(topic) sync switch: checkout topic
		(main) checkout: moving from topic to main
		EOF
		git log -g -2 --format="(%gb) %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb names a detached HEAD by the commit it was detached at' '
	(
		cd branches &&
		git switch --detach topic~1 &&
		detached_at=$(git rev-parse --short HEAD) &&
		test_commit detached &&
		git rebase topic &&
		rebased=$(git rev-parse HEAD) &&
		git switch topic &&

		rebased_at=$(git rev-parse --short $rebased) &&
		cat >expect <<-EOF &&
		(topic) checkout: moving from $rebased to topic
		(detached at $rebased_at) rebase (pick): detached
		(detached at $rebased_at) rebase (start): checkout topic
		(detached at $detached_at) commit: detached
		(detached at $detached_at) checkout: moving from topic to topic~1
		EOF
		git log -g -5 --format="(%gb) %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb names a detached rebase by the commit it ended on' '
	write_script add-exec <<-\EOF &&
	{ echo "exec true" && cat "$1"; } >"$1.new" &&
	mv "$1.new" "$1"
	EOF
	(
		cd branches &&
		git switch --detach topic^0 &&
		test_set_sequence_editor "$TRASH_DIRECTORY/add-exec" &&
		git rebase -i @^ &&
		ended_at=$(git rev-parse --short HEAD) &&
		test_commit after-fast-forward &&

		cat >expect <<-EOF &&
		(detached at $ended_at) commit: after-fast-forward
		(detached at $ended_at) rebase: fast-forward
		(detached at $ended_at) rebase (start): checkout @^
		EOF
		git log -g -3 --format="(%gb) %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb names an aborted rebase of a detached HEAD' '
	(
		cd branches &&
		git switch --detach topic^0 &&
		test_must_fail git rebase -f -x false @^ &&
		git rebase --abort &&
		returned_to=$(git rev-parse HEAD) &&
		short=$(git rev-parse --short HEAD) &&

		cat >expect <<-EOF &&
		(detached at $short) rebase (abort): returning to $returned_to
		(detached at $short) rebase (pick): B
		(detached at $short) rebase (start): checkout @^
		EOF
		git log -g -3 --format="(%gb) %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success '%gb follows a renamed branch' '
	(
		cd branches &&
		git switch topic &&
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

test_expect_success '%gb labels branches and detached HEADs' '
	test_create_repo origin &&
	(
		cd origin &&
		test_commit O1 &&
		test_commit O2
	) &&
	git clone origin downstream &&
	(
		cd origin &&
		test_commit O3
	) &&
	(
		cd downstream &&
		test_commit L1 &&
		git branch side &&
		test_commit L2 &&
		git pull -r "file://$TRASH_DIRECTORY/origin" &&
		test_commit L3 &&
		GIT_REFLOG_ACTION="sync switch" git rebase -i FETCH_HEAD side^0 &&
		H1=$(git rev-parse --short HEAD) &&
		test_commit "checkout main" L4.t L4 L4 &&
		git rebase FETCH_HEAD side &&
		test_commit L5 &&
		git rebase origin/main origin/main &&
		H2=$(git rev-parse --short HEAD) &&
		test_commit L6 &&
		git rebase main refs/heads/main &&
		H3=$(git rev-parse --short HEAD) &&
		test_commit L7 &&
		git switch side &&
		test_commit L8 &&
		git checkout origin/main &&
		H4=$(git rev-parse --short HEAD) &&
		test_commit L9 &&

		cat >expect <<-EOF &&
		commit: L9 (detached at $H4)
		checkout: moving from side to origin/main (detached at $H4)
		commit: L8 (side)
		checkout: moving from $(git rev-parse L7) to side (side)
		commit: L7 (detached at $H3)
		rebase: checkout refs/heads/main (detached at $H3)
		commit: L6 (detached at $H2)
		rebase: checkout origin/main (detached at $H2)
		commit: L5 (side)
		rebase (finish): returning to refs/heads/side (side)
		rebase (pick): L1 (side)
		rebase (start): checkout FETCH_HEAD (side)
		commit: checkout main (detached at $H1)
		sync switch (pick): L1 (detached at $H1)
		sync switch (start): checkout FETCH_HEAD (detached at $H1)
		commit: L3 (main)
		pull -r file://$TRASH_DIRECTORY/origin (finish): returning to refs/heads/main (main)
		pull -r file://$TRASH_DIRECTORY/origin (pick): L2 (main)
		pull -r file://$TRASH_DIRECTORY/origin (pick): L1 (main)
		pull -r file://$TRASH_DIRECTORY/origin (start): checkout $(git rev-parse FETCH_HEAD) (main)
		commit: L2 (main)
		commit: L1 (main)
		clone: from $(cd ../origin && pwd) (main)
		EOF

		git log -g --format="%gs (%gb)" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'rev-list -g complains when there are no reflogs' '
	test_must_fail git rev-list -g
'

test_done
