#!/bin/sh

test_description='performance of partial clones'
. ./perf-lib.sh

test_perf_default_repo

test_expect_success 'enable server-side config' '
	but config uploadpack.allowFilter true &&
	but config uploadpack.allowAnySHA1InWant true
'

test_perf 'clone without blobs' '
	rm -rf bare.but &&
	but clone --no-local --bare --filter=blob:none . bare.but
'

test_perf 'checkout of result' '
	rm -rf worktree &&
	mkdir -p worktree/.but &&
	tar -C bare.but -cf - . | tar -C worktree/.but -xf - &&
	but -C worktree config core.bare false &&
	but -C worktree checkout -f
'

test_perf 'fsck' '
	but -C bare.but fsck
'

test_perf 'count cummits' '
	but -C bare.but rev-list --all --count
'

test_perf 'count non-promisor cummits' '
	but -C bare.but rev-list --all --count --exclude-promisor-objects
'

test_perf 'gc' '
	but -C bare.but gc
'

test_done
