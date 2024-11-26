#!/bin/sh

test_description='check random commands outside repo'

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

test_expect_success 'imap-send outside repository' '
	test_config_global imap.host imaps://localhost &&
	test_config_global imap.folder Drafts &&

	echo nothing to send >expect &&
	test_must_fail git imap-send -v </dev/null 2>actual &&
	test_cmp expect actual &&

	(
		cd non-repo &&
		test_must_fail git imap-send -v </dev/null 2>../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'check-ref-format outside repository' '
	git check-ref-format --branch refs/heads/xyzzy >expect &&
	nongit git check-ref-format --branch refs/heads/xyzzy >actual &&
	test_cmp expect actual
'

test_expect_success 'diff outside repository' '
	echo one >one &&
	echo two >two &&
	test_must_fail git diff --no-index one two >expect.raw &&
	(
		cd non-repo &&
		cp ../one . &&
		cp ../two . &&
		test_must_fail git diff one two >../actual.raw
	) &&
	# outside repository diff falls back to SHA-1 but
	# GIT_DEFAULT_HASH may be set to sha256 on the in-repo side.
	sed -e "/^index /d" expect.raw >expect &&
	sed -e "/^index /d" actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'stripspace outside repository' '
	nongit git stripspace -s </dev/null
'

test_expect_success LIBCURL 'remote-http outside repository' '
	test_must_fail git remote-http 2>actual &&
	test_grep "^error: remote-curl" actual &&
	(
		cd non-repo &&
		test_must_fail git remote-http 2>../actual
	) &&
	test_grep "^error: remote-curl" actual
'

test_done
