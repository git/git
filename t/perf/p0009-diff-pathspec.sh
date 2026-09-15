#!/bin/sh

test_description='Tests performance of diffing the working tree with a pathspec'

. ./perf-lib.sh

test_perf_fresh_repo

count=10000
if test_have_prereq EXPENSIVE
then
	count=100000
fi

# The entries exist only in the index, which is enough to
# exercise the index scan.
test_expect_success 'setup' '
	blob=$(echo content | git hash-object -w --stdin) &&
	{
		printf "100644 $blob\taaa/file\n" &&
		printf "100644 $blob\tf%s\n" $(test_seq $count)
	} | git update-index --index-info &&
	git commit -q -m initial &&
	mkdir -p aaa &&
	echo content >aaa/file
'

test_perf 'diff pathspec subtree' '
	git diff HEAD -- aaa/file
'

test_done
