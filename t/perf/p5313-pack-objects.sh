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

for version in 1 2
do
	export version

	test_perf "thin pack with version $version" '
		git pack-objects --thin --stdout --revs --sparse \
			--name-hash-version=$version <in-thin >out
	'

	test_size "thin pack size with version $version" '
		test_file_size out
	'

	test_perf "big pack with version $version" '
		git pack-objects --stdout --revs --sparse \
			--name-hash-version=$version <in-big >out
	'

	test_size "big pack size with version $version" '
		test_file_size out
	'

	test_perf "shallow fetch pack with version $version" '
		git pack-objects --stdout --revs --sparse --shallow \
			--name-hash-version=$version <in-shallow >out
	'

	test_size "shallow pack size with version $version" '
		test_file_size out
	'

	test_perf "repack with version $version" '
		git repack -adf --name-hash-version=$version
	'

	test_size "repack size with version $version" '
		gitdir=$(git rev-parse --git-dir) &&
		pack=$(ls $gitdir/objects/pack/pack-*.pack) &&
		test_file_size "$pack"
	'
done

test_done
