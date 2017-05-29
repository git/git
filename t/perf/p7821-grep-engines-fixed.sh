#!/bin/sh

test_description="Comparison of git-grep's regex engines with -F

Set GIT_PERF_7821_GREP_OPTS in the environment to pass options to
git-grep. Make sure to include a leading space,
e.g. GIT_PERF_7821_GREP_OPTS=' -w'. See p7820-grep-engines.sh for more
options to try.
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
		test_perf $prereq "$engine grep$GIT_PERF_7821_GREP_OPTS $pattern" "
			git -c grep.patternType=$engine grep$GIT_PERF_7821_GREP_OPTS $pattern >'out.$engine' || :
		"
	done

	test_expect_success "assert that all engines found the same for$GIT_PERF_7821_GREP_OPTS $pattern" '
		test_cmp out.fixed out.basic &&
		test_cmp out.fixed out.extended &&
		if test_have_prereq PCRE
		then
			test_cmp out.fixed out.perl
		fi
	'
done

test_done
