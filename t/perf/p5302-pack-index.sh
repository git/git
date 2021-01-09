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

# Rather than counting up and doubling each time, count down from the endpoint,
# halving each time. That ensures that our final test uses as many threads as
# CPUs, even if it isn't a power of 2.
test_expect_success 'set up thread-counting tests' '
	t=$(test-tool online-cpus) &&
	threads= &&
	while test $t -gt 0
	do
		threads="$t $threads"
		t=$((t / 2))
	done
'

test_perf PERF_EXTRA 'index-pack 0 threads' '
	rm -rf repo.git &&
	git init --bare repo.git &&
	GIT_DIR=repo.git git index-pack --threads=1 --stdin < $PACK
'

for t in $threads
do
	THREADS=$t
	export THREADS
	test_perf PERF_EXTRA "index-pack $t threads" '
		rm -rf repo.git &&
		git init --bare repo.git &&
		GIT_DIR=repo.git GIT_FORCE_THREADS=1 \
		git index-pack --threads=$THREADS --stdin <$PACK
	'
done

test_perf 'index-pack default number of threads' '
	rm -rf repo.git &&
	git init --bare repo.git &&
	GIT_DIR=repo.git git index-pack --stdin < $PACK
'

test_done
