#!/bin/sh

test_description="git-grep's perl regex

If GIT_PERF_GREP_THREADS is set to a list of threads (e.g. '1 4 8'
etc.) we will test the patterns under those numbers of threads.
"

. ./perf-lib.sh

# setting a LOCALE is needed, but not yet supported by :
#. "$TEST_DIRECTORY"/lib-gettext.sh

# Invoke like:
#
# LC_ALL=is_IS.utf8 ./p7822-grep-perl-character.sh

test_perf_large_repo
test_checkout_worktree

if test -n "$GIT_PERF_GREP_THREADS"
then
	test_set_prereq PERF_GREP_ENGINES_THREADS
fi

for pattern in \
	'\\bhow' \
	'\\bÆvar' \
	'\\d+ \\bÆvar' \
	'\\bBelón\\b' \
	'\\w{12}\\b'
do
	echo '$pattern' >pat
	if ! test_have_prereq PERF_GREP_ENGINES_THREADS
	then
		test_perf "grep -P '$pattern'" --prereq PCRE "
			git grep -P -f pat || :
		"
	else
		for threads in $GIT_PERF_GREP_THREADS
		do
			test_perf "grep -P '$pattern' with $threads threads" --prereq PTHREADS,PCRE "
				git -c grep.threads=$threads grep -P -f pat || :
			"
		done
	fi
done

test_done
