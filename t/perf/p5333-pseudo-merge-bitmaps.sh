#!/bin/sh

test_description='pseudo-merge bitmaps'
. ./perf-lib.sh

test_perf_large_repo

test_expect_success 'setup' '
	git \
		-c bitmapPseudoMerge.all.pattern="refs/" \
		-c bitmapPseudoMerge.all.threshold=now \
		-c bitmapPseudoMerge.all.stableThreshold=never \
		-c bitmapPseudoMerge.all.maxMerges=64 \
		-c pack.writeBitmapLookupTable=true \
		repack -adb
'

test_perf 'git rev-list --count --all --objects (no bitmaps)' '
	git rev-list --objects --all
'

test_perf 'git rev-list --count --all --objects (no pseudo-merges)' '
	GIT_TEST_USE_PSEUDO_MERGES=0 \
		git rev-list --objects --all --use-bitmap-index
'

test_perf 'git rev-list --count --all --objects (with pseudo-merges)' '
	GIT_TEST_USE_PSEUDO_MERGES=1 \
		git rev-list --objects --all --use-bitmap-index
'

test_done
