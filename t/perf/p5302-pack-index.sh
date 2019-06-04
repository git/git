#!/bin/sh

test_description="Tests index-pack performance"

. ./perf-lib.sh

test_perf_large_repo

test_expect_success 'repack' '
	git repack -ad &&
	PACK=$(ls .git/objects/pack/*.pack | head -n1) &&
	test -f "$PACK" &&
	export PACK
'

test_perf 'index-pack 0 threads' '
	rm -rf repo.git &&
	git init --bare repo.git &&
	GIT_DIR=repo.git git index-pack --threads=1 --stdin < $PACK
'

test_perf 'index-pack 1 thread ' '
	rm -rf repo.git &&
	git init --bare repo.git &&
	GIT_DIR=repo.git GIT_FORCE_THREADS=1 git index-pack --threads=1 --stdin < $PACK
'

test_perf 'index-pack 2 threads' '
	rm -rf repo.git &&
	git init --bare repo.git &&
	GIT_DIR=repo.git git index-pack --threads=2 --stdin < $PACK
'

test_perf 'index-pack 4 threads' '
	rm -rf repo.git &&
	git init --bare repo.git &&
	GIT_DIR=repo.git git index-pack --threads=4 --stdin < $PACK
'

test_perf 'index-pack 8 threads' '
	rm -rf repo.git &&
	git init --bare repo.git &&
	GIT_DIR=repo.git git index-pack --threads=8 --stdin < $PACK
'

test_perf 'index-pack default number of threads' '
	rm -rf repo.git &&
	git init --bare repo.git &&
	GIT_DIR=repo.git git index-pack --stdin < $PACK
'

test_done
