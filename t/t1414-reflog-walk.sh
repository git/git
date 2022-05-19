#!/bin/sh

test_description='various tests of reflog walk (log -g) behavior'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'set up some reflog entries' '
	test_cummit one &&
	test_cummit two &&
	but checkout -b side HEAD^ &&
	test_cummit three &&
	but merge --no-cummit main &&
	echo evil-merge-content >>one.t &&
	test_tick &&
	but cummit --no-edit -a
'

do_walk () {
	but log -g --format="%gd %gs" "$@"
}

test_expect_success 'set up expected reflog' '
	cat >expect.all <<-EOF
	HEAD@{0} cummit (merge): Merge branch ${SQ}main${SQ} into side
	HEAD@{1} cummit: three
	HEAD@{2} checkout: moving from main to side
	HEAD@{3} cummit: two
	HEAD@{4} cummit (initial): one
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
	#   - the initial cummit of one
	#   - the checkout back to cummit one
	#   - the evil merge which touched one
	sed -n "1p;3p;5p" expect.all >expect &&
	do_walk -- one.t >actual &&
	test_cmp expect actual
'

test_expect_success '--parents shows true parents' '
	# convert newlines to spaces
	echo $(but rev-parse HEAD HEAD^1 HEAD^2) >expect &&
	but rev-list -g --parents -1 HEAD >actual &&
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
	# Flip between cummits one and two so each ref update actually
	# does something (and does not get optimized out). We know
	# that the timestamps of those cummits will be before our "min".

	but update-ref -m before refs/heads/minmax one &&

	test_tick &&
	min=$test_tick &&
	but update-ref -m min refs/heads/minmax two &&

	test_tick &&
	max=$test_tick &&
	but update-ref -m max refs/heads/minmax one &&

	test_tick &&
	but update-ref -m after refs/heads/minmax two &&

	cat >expect <<-\EOF &&
	max
	min
	EOF
	but log -g --since=$min --until=$max --format=%gs minmax >actual &&
	test_cmp expect actual
'

# Create a situation where the reflog and ref database disagree about the latest
# state of HEAD.
test_expect_success REFFILES 'walk prefers reflog to ref tip' '
	head=$(but rev-parse HEAD) &&
	one=$(but rev-parse one) &&
	ident="$GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL> $GIT_CUMMITTER_DATE" &&
	echo "$head $one $ident	broken reflog entry" >>.but/logs/HEAD &&

	echo $one >expect &&
	but log -g --format=%H -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-list -g complains when there are no reflogs' '
	test_must_fail but rev-list -g
'

test_done
