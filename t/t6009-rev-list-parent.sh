#!/bin/sh

test_description='ancestor culling and limiting by parent number'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_revlist () {
	rev_list_args="$1" &&
	shift &&
	but rev-parse "$@" >expect &&
	but rev-list $rev_list_args --all >actual &&
	test_cmp expect actual
}

test_expect_success setup '

	touch file &&
	but add file &&

	test_cummit one &&

	test_tick=$(($test_tick - 2400)) &&

	test_cummit two &&
	test_cummit three &&
	test_cummit four &&

	but log --pretty=oneline --abbrev-cummit
'

test_expect_success 'one is ancestor of others and should not be shown' '

	but rev-list one --not four >result &&
	test_must_be_empty result

'

test_expect_success 'setup roots, merges and octopuses' '

	but checkout --orphan newroot &&
	test_cummit five &&
	but checkout -b sidebranch two &&
	test_cummit six &&
	but checkout -b anotherbranch three &&
	test_cummit seven &&
	but checkout -b yetanotherbranch four &&
	test_cummit eight &&
	but checkout main &&
	test_tick &&
	but merge --allow-unrelated-histories -m normalmerge newroot &&
	but tag normalmerge &&
	test_tick &&
	but merge -m tripus sidebranch anotherbranch &&
	but tag tripus &&
	but checkout -b tetrabranch normalmerge &&
	test_tick &&
	but merge -m tetrapus sidebranch anotherbranch yetanotherbranch &&
	but tag tetrapus &&
	but checkout main
'

test_expect_success 'rev-list roots' '

	check_revlist "--max-parents=0" one five
'

test_expect_success 'rev-list no merges' '

	check_revlist "--max-parents=1" one eight seven six five four three two &&
	check_revlist "--no-merges" one eight seven six five four three two
'

test_expect_success 'rev-list no octopuses' '

	check_revlist "--max-parents=2" one normalmerge eight seven six five four three two
'

test_expect_success 'rev-list no roots' '

	check_revlist "--min-parents=1" tetrapus tripus normalmerge eight seven six four three two
'

test_expect_success 'rev-list merges' '

	check_revlist "--min-parents=2" tetrapus tripus normalmerge &&
	check_revlist "--merges" tetrapus tripus normalmerge
'

test_expect_success 'rev-list octopus' '

	check_revlist "--min-parents=3" tetrapus tripus
'

test_expect_success 'rev-list ordinary cummits' '

	check_revlist "--min-parents=1 --max-parents=1" eight seven six four three two
'

test_expect_success 'rev-list --merges --no-merges yields empty set' '

	check_revlist "--min-parents=2 --no-merges" &&
	check_revlist "--merges --no-merges" &&
	check_revlist "--no-merges --merges"
'

test_expect_success 'rev-list override and infinities' '

	check_revlist "--min-parents=2 --max-parents=1 --max-parents=3" tripus normalmerge &&
	check_revlist "--min-parents=1 --min-parents=2 --max-parents=7" tetrapus tripus normalmerge &&
	check_revlist "--min-parents=2 --max-parents=8" tetrapus tripus normalmerge &&
	check_revlist "--min-parents=2 --max-parents=-1" tetrapus tripus normalmerge &&
	check_revlist "--min-parents=2 --no-max-parents" tetrapus tripus normalmerge &&
	check_revlist "--max-parents=0 --min-parents=1 --no-min-parents" one five
'

test_expect_success 'dodecapus' '

	roots= &&
	for i in 1 2 3 4 5 6 7 8 9 10 11
	do
		but checkout -b root$i five &&
		test_cummit $i &&
		roots="$roots root$i" ||
		return 1
	done &&
	but checkout main &&
	test_tick &&
	but merge -m dodecapus $roots &&
	but tag dodecapus &&

	check_revlist "--min-parents=4" dodecapus tetrapus &&
	check_revlist "--min-parents=8" dodecapus &&
	check_revlist "--min-parents=12" dodecapus &&
	check_revlist "--min-parents=13" &&
	check_revlist "--min-parents=4 --max-parents=11" tetrapus
'

test_expect_success 'ancestors with the same cummit time' '

	test_tick_keep=$test_tick &&
	for i in 1 2 3 4 5 6 7 8; do
		test_tick=$test_tick_keep &&
		test_cummit t$i || return 1
	done &&
	but rev-list t1^! --not t$i >result &&
	test_must_be_empty result
'

test_done
