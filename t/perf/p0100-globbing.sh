#!/bin/sh

test_description="Tests pathological globbing performance

Shows how Git's globbing performance performs when given the sort of
pathological patterns described in at https://research.swtch.com/glob
"

. ./perf-lib.sh

test_globs_big='10 25 50 75 100'
test_globs_small='1 2 3 4 5 6'

test_perf_fresh_repo

test_expect_success 'setup' '
	for i in $(test_seq 1 100)
	do
		printf "a" >>refname &&
		for j in $(test_seq 1 $i)
		do
			printf "a*" >>refglob.$i
		done &&
		echo b >>refglob.$i
	done &&
	test_commit test $(cat refname).t "" $(cat refname).t
'

for i in $test_globs_small
do
	test_perf "refglob((a*)^nb) against tag (a^100).t; n = $i" '
		git for-each-ref "refs/tags/$(cat refglob.'$i')b"
	'
done

for i in $test_globs_small
do
	test_perf "fileglob((a*)^nb) against file (a^100).t; n = $i" '
		git ls-files "$(cat refglob.'$i')b"
	'
done

test_done
