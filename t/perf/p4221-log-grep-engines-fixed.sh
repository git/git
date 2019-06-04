#!/bin/sh

test_description="Comparison of git-log's --grep regex engines with -F

Set GIT_PERF_4221_LOG_OPTS in the environment to pass options to
git-grep. Make sure to include a leading space,
e.g. GIT_PERF_4221_LOG_OPTS=' -i'. Some options to try:

	-i
	--invert-grep
	-i --invert-grep
"

. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

for pattern in 'int' 'uncommon' 'æ'
do
	for engine in fixed basic extended perl
	do
		if test $engine = "perl" && ! test_have_prereq PCRE
		then
			prereq="PCRE"
		else
			prereq=""
		fi
		test_perf $prereq "$engine log$GIT_PERF_4221_LOG_OPTS --grep='$pattern'" "
			git -c grep.patternType=$engine log --pretty=format:%h$GIT_PERF_4221_LOG_OPTS --grep='$pattern' >'out.$engine' || :
		"
	done

	test_expect_success "assert that all engines found the same for$GIT_PERF_4221_LOG_OPTS '$pattern'" '
		test_cmp out.fixed out.basic &&
		test_cmp out.fixed out.extended &&
		if test_have_prereq PCRE
		then
			test_cmp out.fixed out.perl
		fi
	'
done

test_done
