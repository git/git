#!/bin/sh

test_description="Performance tests for git stash -p"

. ./perf-lib.sh

test_perf_fresh_repo

test_expect_success "setup" '
	mkdir files &&
	test_seq 1 100000 | while read i; do
		echo "content $i" >files/$i.txt || return 1
	done &&
	git add files/ &&
	git commit -q -m "add tracked files" &&
	echo modified >files/1.txt
'

test_perf "stash -p, no fsmonitor" \
	--setup 'echo modified >files/1.txt' '
	printf "q\n" | git stash -p >/dev/null 2>&1 || true
'

if test_have_prereq FSMONITOR_DAEMON
then
	test_expect_success "enable builtin fsmonitor" '
		git config core.fsmonitor true &&
		git fsmonitor--daemon start &&
		git update-index --fsmonitor &&
		git status >/dev/null 2>&1
	'

	test_perf "stash -p, builtin fsmonitor" \
		--setup 'echo modified >files/1.txt && git status >/dev/null 2>&1' '
		printf "q\n" | git stash -p >/dev/null 2>&1 || true
	'

	test_expect_success "stop builtin fsmonitor" '
		git fsmonitor--daemon stop
	'
fi

test_done
