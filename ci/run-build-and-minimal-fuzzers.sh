#!/bin/sh
#
# Build and test Git's fuzzers
#

. ${0%/*}/lib.sh

group "Build fuzzers" make \
	NO_CURL=NoThanks \
	CC=clang \
	FUZZ_CXX=clang++ \
	CFLAGS="-fsanitize=fuzzer-no-link,address" \
	LIB_FUZZING_ENGINE="-fsanitize=fuzzer,address" \
	fuzz-all

for fuzzer in commit-graph config date pack-headers pack-idx ; do
	begin_group "fuzz-$fuzzer"
	./oss-fuzz/fuzz-$fuzzer -verbosity=0 -runs=1 || exit 1
	end_group "fuzz-$fuzzer"
done
