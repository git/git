#!/bin/sh

test_description='Tests ls-files worktree performance'

. ./perf-lib.sh

test_perf_large_repo
test_checkout_worktree

test_expect_success 'select a zero-prefix pathspec' '
	tracked_file=$(git ls-files | sed -n 1p) &&
	test -n "$tracked_file" &&
	pathspec="?${tracked_file#?}" &&
	test_export pathspec
'

test_perf 'ls-files --deleted with pathspec' '
	git -c core.fsmonitor=false ls-files --deleted \
		-- "$pathspec" >/dev/null
'

test_perf 'ls-files --deleted with all-matching pathspec' '
	git -c core.fsmonitor=false ls-files --deleted -- "*" >/dev/null
'

test_perf 'ls-files --modified with pathspec' '
	git -c core.fsmonitor=false ls-files --modified \
		-- "$pathspec" >/dev/null
'

test_perf 'ls-files --others with pathspec and no untracked cache' '
	git -c core.fsmonitor=false -c core.untrackedCache=false \
		ls-files --cached --others --exclude-standard -- "$pathspec" >/dev/null
'

test_expect_success 'populate the untracked cache with ls-files' '
	git config core.untrackedCache true &&
	git -c core.fsmonitor=false ls-files --others --exclude-standard >/dev/null
'

test_perf 'ls-files --others with pathspec and untracked cache' '
	git -c core.fsmonitor=false ls-files --cached --others \
		--exclude-standard -- "$pathspec" >/dev/null
'

test_done
