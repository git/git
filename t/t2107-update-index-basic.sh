#!/bin/sh

test_description='basic update-index tests

Tests for command-line parsing and basic operation.
'

. ./test-lib.sh

test_expect_success 'update-index --nonsense fails' '
	test_must_fail but update-index --nonsense 2>msg &&
	test -s msg
'

test_expect_success 'update-index --nonsense dumps usage' '
	test_expect_code 129 but update-index --nonsense 2>err &&
	test_i18ngrep "[Uu]sage: but update-index" err
'

test_expect_success 'update-index -h with corrupt index' '
	mkdir broken &&
	(
		cd broken &&
		but init &&
		>.but/index &&
		test_expect_code 129 but update-index -h >usage 2>&1
	) &&
	test_i18ngrep "[Uu]sage: but update-index" broken/usage
'

test_expect_success '--cacheinfo complains of missing arguments' '
	test_must_fail but update-index --cacheinfo
'

test_expect_success '--cacheinfo does not accept blob null sha1' '
	echo content >file &&
	but add file &&
	but rev-parse :file >expect &&
	test_must_fail but update-index --cacheinfo 100644 $ZERO_OID file &&
	but rev-parse :file >actual &&
	test_cmp expect actual
'

test_expect_success '--cacheinfo does not accept butlink null sha1' '
	but init submodule &&
	(cd submodule && test_cummit foo) &&
	but add submodule &&
	but rev-parse :submodule >expect &&
	test_must_fail but update-index --cacheinfo 160000 $ZERO_OID submodule &&
	but rev-parse :submodule >actual &&
	test_cmp expect actual
'

test_expect_success '--cacheinfo mode,sha1,path (new syntax)' '
	echo content >file &&
	but hash-object -w --stdin <file >expect &&

	but update-index --add --cacheinfo 100644 "$(cat expect)" file &&
	but rev-parse :file >actual &&
	test_cmp expect actual &&

	but update-index --add --cacheinfo "100644,$(cat expect),elif" &&
	but rev-parse :elif >actual &&
	test_cmp expect actual
'

test_expect_success '.lock files cleaned up' '
	mkdir cleanup &&
	(
	cd cleanup &&
	mkdir worktree &&
	but init repo &&
	cd repo &&
	but config core.worktree ../../worktree &&
	# --refresh triggers late setup_work_tree,
	# active_cache_changed is zero, rollback_lock_file fails
	but update-index --refresh &&
	! test -f .but/index.lock
	)
'

test_expect_success '--chmod=+x and chmod=-x in the same argument list' '
	>A &&
	>B &&
	but add A B &&
	but update-index --chmod=+x A --chmod=-x B &&
	cat >expect <<-EOF &&
	100755 $EMPTY_BLOB 0	A
	100644 $EMPTY_BLOB 0	B
	EOF
	but ls-files --stage A B >actual &&
	test_cmp expect actual
'

test_done
