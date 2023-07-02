#!/bin/sh

test_description="Comparison of git-log's --grep regex engines

Set GIT_PERF_4220_LOG_OPTS in the environment to pass options to
git-grep. Make sure to include a leading space,
e.g. GIT_PERF_4220_LOG_OPTS=' -i'. Some options to try:

	-i
	--invert-grep
	-i --invert-grep
"

. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

for pattern in \
	'how.to' \
	'^how to' \
	'[how] to' \
	'\(e.t[^ ]*\|v.ry\) rare' \
	'm\(ú\|u\)lt.b\(æ\|y\)te'
do
	for engine in basic extended perl
	do
		if test $engine != "basic"
		then
			# Poor man's basic -> extended converter.
			pattern=$(echo $pattern | sed 's/\\//g')
		fi
		if test $engine = "perl" && ! test_have_prereq PCRE
		then
			prereq="PCRE"
		else
			prereq=""
		fi
		test_perf "$engine log$GIT_PERF_4220_LOG_OPTS --grep='$pattern'" \
			--prereq "$prereq" "
			git -c grep.patternType=$engine log --pretty=format:%h$GIT_PERF_4220_LOG_OPTS --grep='$pattern' >'out.$engine' || :
		"
	done

	test_expect_success "assert that all engines found the same for$GIT_PERF_4220_LOG_OPTS '$pattern'" '
		test_cmp out.basic out.extended &&
		if test_have_prereq PCRE
		then
			test_cmp out.basic out.perl
		fi
	'
done

test_done
