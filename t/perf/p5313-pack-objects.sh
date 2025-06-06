#!/bin/sh

test_description='Tests pack performance using bitmaps'
. ./perf-lib.sh

test_perf_large_repo

test_expect_success 'create rev input' '
	cat >in-thin <<-EOF &&
	$(git rev-parse HEAD)
	^$(git rev-parse HEAD~1)
	EOF

	cat >in-big <<-EOF &&
	$(git rev-parse HEAD)
	^$(git rev-parse HEAD~1000)
	EOF

	cat >in-shallow <<-EOF
	$(git rev-parse HEAD)
	--shallow $(git rev-parse HEAD)
	EOF
'

test_all_with_args () {
	parameter=$1
	export parameter

	test_perf "thin pack with $parameter" '
		git pack-objects --thin --stdout --revs --sparse \
			$parameter <in-thin >out
	'

	test_size "thin pack size with $parameter" '
		test_file_size out
	'

	test_perf "big pack with $parameter" '
		git pack-objects --stdout --revs --sparse \
			$parameter <in-big >out
	'

	test_size "big pack size with $parameter" '
		test_file_size out
	'

	test_perf "shallow fetch pack with $parameter" '
		git pack-objects --stdout --revs --sparse --shallow \
			$parameter <in-shallow >out
	'

	test_size "shallow pack size with $parameter" '
		test_file_size out
	'

	test_perf "repack with $parameter" '
		git repack -adf $parameter
	'

	test_size "repack size with $parameter" '
		gitdir=$(git rev-parse --git-dir) &&
		pack=$(ls $gitdir/objects/pack/pack-*.pack) &&
		test_file_size "$pack"
	'
}

for version in 1 2
do
	test_all_with_args --name-hash-version=$version
done

test_all_with_args --path-walk

test_done
