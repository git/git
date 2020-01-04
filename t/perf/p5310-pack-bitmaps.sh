#!/bin/sh

test_description='Tests pack performance using bitmaps'
. ./perf-lib.sh

test_perf_large_repo

# note that we do everything through config,
# since we want to be able to compare bitmap-aware
# git versus non-bitmap git
#
# We intentionally use the deprecated pack.writebitmaps
# config so that we can test against older versions of git.
test_expect_success 'setup bitmap config' '
	git config pack.writebitmaps true
'

test_perf 'repack to disk' '
	git repack -ad
'

test_perf 'simulated clone' '
	git pack-objects --stdout --all </dev/null >/dev/null
'

test_perf 'simulated fetch' '
	have=$(git rev-list HEAD~100 -1) &&
	{
		echo HEAD &&
		echo ^$have
	} | git pack-objects --revs --stdout >/dev/null
'

test_perf 'pack to file' '
	git pack-objects --all pack1 </dev/null >/dev/null
'

test_perf 'pack to file (bitmap)' '
	git pack-objects --use-bitmap-index --all pack1b </dev/null >/dev/null
'

test_expect_success 'create partial bitmap state' '
	# pick a commit to represent the repo tip in the past
	cutoff=$(git rev-list HEAD~100 -1) &&
	orig_tip=$(git rev-parse HEAD) &&

	# now kill off all of the refs and pretend we had
	# just the one tip
	rm -rf .git/logs .git/refs/* .git/packed-refs &&
	git update-ref HEAD $cutoff &&

	# and then repack, which will leave us with a nice
	# big bitmap pack of the "old" history, and all of
	# the new history will be loose, as if it had been pushed
	# up incrementally and exploded via unpack-objects
	git repack -Ad &&

	# and now restore our original tip, as if the pushes
	# had happened
	git update-ref HEAD $orig_tip
'

test_perf 'clone (partial bitmap)' '
	git pack-objects --stdout --all </dev/null >/dev/null
'

test_perf 'pack to file (partial bitmap)' '
	git pack-objects --use-bitmap-index --all pack2b </dev/null >/dev/null
'

test_done
