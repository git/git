#!/bin/sh

test_description='performance of partial clones'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'enable server-side config' '
	git config uploadpack.allowFilter true &&
	git config uploadpack.allowAnySHA1InWant true
'

test_perf 'clone without blobs' '
	rm -rf bare.git &&
	git clone --no-local --bare --filter=blob:none . bare.git
'

test_perf 'checkout of result' '
	rm -rf worktree &&
	mkdir -p worktree/.git &&
	tar -C bare.git -cf - . | tar -C worktree/.git -xf - &&
	git -C worktree config core.bare false &&
	git -C worktree checkout -f
'

test_perf 'fsck' '
	git -C bare.git fsck
'

test_perf 'count commits' '
	git -C bare.git rev-list --all --count
'

test_perf 'count non-promisor commits' '
	git -C bare.git rev-list --all --count --exclude-promisor-objects
'

test_perf 'gc' '
	git -C bare.git gc
'

test_done
