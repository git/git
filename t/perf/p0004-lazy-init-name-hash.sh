#!/bin/sh

test_description='Tests multi-threaded lazy_init_name_hash'
. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

test_expect_success 'verify both methods build the same hashmaps' '
	test-tool lazy-init-name-hash --dump --single >out.single &&
	if test-tool lazy-init-name-hash --dump --multi >out.multi
	then
		test_set_prereq REPO_BIG_ENOUGH_FOR_MULTI &&
		sort <out.single >sorted.single &&
		sort <out.multi >sorted.multi &&
		test_cmp sorted.single sorted.multi
	fi
'

test_expect_success 'calibrate' '
	entries=$(wc -l <out.single) &&

	case $entries in
	?) count=1000000 ;;
	??) count=100000 ;;
	???) count=10000 ;;
	????) count=1000 ;;
	?????) count=100 ;;
	??????) count=10 ;;
	*) count=1 ;;
	esac &&
	export count &&

	case $entries in
	1) entries_desc="1 entry" ;;
	*) entries_desc="$entries entries" ;;
	esac &&

	case $count in
	1) count_desc="1 round" ;;
	*) count_desc="$count rounds" ;;
	esac &&

	desc="$entries_desc, $count_desc" &&
	export desc
'

test_perf "single-threaded, $desc" "
	test-tool lazy-init-name-hash --single --count=$count
"

test_perf "multi-threaded, $desc" --prereq REPO_BIG_ENOUGH_FOR_MULTI "
	test-tool lazy-init-name-hash --multi --count=$count
"

test_done
