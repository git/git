#!/bin/sh

test_description='check random commands outside repo'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'set up a non-repo directory and test file' '
	GIT_CEILING_DIRECTORIES=$(pwd) &&
	export GIT_CEILING_DIRECTORIES &&
	mkdir non-repo &&
	(
		cd non-repo &&
		# confirm that git does not find a repo
		test_must_fail git rev-parse --git-dir
	) &&
	test_write_lines one two three four >nums &&
	git add nums &&
	cp nums nums.old &&
	test_write_lines five >>nums &&
	git diff >sample.patch
'

test_expect_success 'compute a patch-id outside repository (uses SHA-1)' '
	nongit env GIT_DEFAULT_HASH=sha1 \
		git patch-id <sample.patch >patch-id.expect &&
	nongit \
		git patch-id <sample.patch >patch-id.actual &&
	test_cmp patch-id.expect patch-id.actual
'

test_expect_success 'hash-object outside repository (uses SHA-1)' '
	nongit env GIT_DEFAULT_HASH=sha1 \
		git hash-object --stdin <sample.patch >hash.expect &&
	nongit \
		git hash-object --stdin <sample.patch >hash.actual &&
	test_cmp hash.expect hash.actual
'

test_expect_success 'apply a patch outside repository' '
	(
		cd non-repo &&
		cp ../nums.old nums &&
		git apply ../sample.patch
	) &&
	test_cmp nums non-repo/nums
'

test_expect_success 'grep outside repository' '
	git grep --cached two >expect &&
	(
		cd non-repo &&
		cp ../nums.old nums &&
		git grep --no-index two >../actual
	) &&
	test_cmp expect actual
'

test_done
