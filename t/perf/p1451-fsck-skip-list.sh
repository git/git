#!/bin/sh

test_description='Test fsck skipList performance'

. ./perf-lib.sh

test_perf_fresh_repo

n=1000000

test_expect_success "setup $n bad commits" '
	for i in $(test_seq 1 $n)
	do
		echo "commit refs/heads/master" &&
		echo "committer C <c@example.com> 1234567890 +0000" &&
		echo "data <<EOF" &&
		echo "$i.Q." &&
		echo "EOF"
	done | q_to_nul | git fast-import
'

skip=0
while test $skip -le $n
do
	test_expect_success "create skipList for $skip bad commits" '
		git log --format=%H --max-count=$skip |
		sort >skiplist
	'

	test_perf "fsck with $skip skipped bad commits" '
		git -c fsck.skipList=skiplist fsck
	'

	case $skip in
	0) skip=1 ;;
	*) skip=${skip}0 ;;
	esac
done

test_done
