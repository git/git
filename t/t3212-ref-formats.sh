#!/bin/sh

test_description='test across ref formats'

GIT_TEST_PACKED_REFS_VERSION=0
export GIT_TEST_PACKED_REFS_VERSION

. ./test-lib.sh

test_expect_success 'extensions.refFormat requires core.repositoryFormatVersion=1' '
	test_when_finished rm -rf broken &&

	# Force sha1 to ensure GIT_TEST_DEFAULT_HASH does
	# not imply a value of core.repositoryFormatVersion.
	git init --object-format=sha1 broken &&
	git -C broken config extensions.refFormat files &&
	test_must_fail git -C broken status 2>err &&
	grep "repo version is 0, but v1-only extension found" err
'

test_expect_success 'invalid extensions.refFormat' '
	test_when_finished rm -rf broken &&
	git init broken &&
	git -C broken config core.repositoryFormatVersion 1 &&
	git -C broken config extensions.refFormat bogus &&
	test_must_fail git -C broken status 2>err &&
	grep "invalid value for '\''extensions.refFormat'\'': '\''bogus'\''" err
'

test_expect_success 'extensions.refFormat=packed only' '
	git init only-packed &&
	(
		cd only-packed &&
		git config core.repositoryFormatVersion 1 &&
		git config extensions.refFormat packed &&
		test_commit A &&
		test_path_exists .git/packed-refs &&
		test_path_is_missing .git/refs/tags/A
	)
'

test_expect_success 'extensions.refFormat=files only' '
	test_commit T &&
	git pack-refs --all &&
	git init only-loose &&
	(
		cd only-loose &&
		git config core.repositoryFormatVersion 1 &&
		git config extensions.refFormat files &&
		test_commit A &&
		test_commit B &&
		test_must_fail git pack-refs 2>err &&
		grep "refusing to create" err &&
		test_path_is_missing .git/packed-refs &&

		# Refuse to parse a packed-refs file.
		cp ../.git/packed-refs .git/packed-refs &&
		test_must_fail git rev-parse refs/tags/T
	)
'

test_expect_success 'extensions.refFormat=files,packed-v2' '
	test_commit Q &&
	git pack-refs --all &&
	git init no-packed-v1 &&
	(
		cd no-packed-v1 &&
		git config core.repositoryFormatVersion 1 &&
		git config extensions.refFormat files &&
		git config --add extensions.refFormat packed-v2 &&
		test_commit A &&
		test_commit B &&

		# Refuse to parse a v1 packed-refs file.
		cp ../.git/packed-refs .git/packed-refs &&
		test_must_fail git rev-parse refs/tags/Q &&
		rm -f .git/packed-refs &&

		git for-each-ref --format="%(refname) %(objectname)" >expect-all &&
		git for-each-ref --format="%(refname) %(objectname)" \
			refs/tags/* >expect-tags &&

		# Create a v2 packed-refs file
		git pack-refs --all &&
		test_path_exists .git/packed-refs &&
		for t in A B
		do
			test_path_is_missing .git/refs/tags/$t &&
			git rev-parse refs/tags/$t || return 1
		done &&

		git for-each-ref --format="%(refname) %(objectname)" >actual-all &&
		test_cmp expect-all actual-all &&
		git for-each-ref --format="%(refname) %(objectname)" \
			refs/tags/* >actual-tags &&
		test_cmp expect-tags actual-tags
	)
'

test_done
