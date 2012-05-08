#!/bin/sh

test_description="Tests index-pack performance"

. ./perf-lib.sh

test_perf_large_repo

test_expect_success 'repack' '
	git repack -ad &&
	PACK=`ls .git/objects/pack/*.pack | head -n1` &&
	test -f "$PACK" &&
	export PACK
'

test_perf 'index-pack 0 threads' '
	GIT_DIR=t1 git index-pack --threads=1 --stdin < $PACK
'

test_perf 'index-pack 1 thread ' '
	GIT_DIR=t2 GIT_FORCE_THREADS=1 git index-pack --threads=1 --stdin < $PACK
'

test_perf 'index-pack 2 threads' '
	GIT_DIR=t3 git index-pack --threads=2 --stdin < $PACK
'

test_perf 'index-pack 4 threads' '
	GIT_DIR=t4 git index-pack --threads=4 --stdin < $PACK
'

test_perf 'index-pack 8 threads' '
	GIT_DIR=t5 git index-pack --threads=8 --stdin < $PACK
'

test_perf 'index-pack default number of threads' '
	GIT_DIR=t6 git index-pack --stdin < $PACK
'

test_done
