#!/bin/sh

test_description='git fetch output format'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'fetch with invalid output format configuration' '
	test_when_finished "rm -rf clone" &&
	git clone . clone &&

	test_must_fail git -C clone -c fetch.output fetch origin 2>actual.err &&
	cat >expect <<-EOF &&
	error: missing value for ${SQ}fetch.output${SQ}
	fatal: unable to parse ${SQ}fetch.output${SQ} from command-line config
	EOF
	test_cmp expect actual.err &&

	test_must_fail git -C clone -c fetch.output= fetch origin 2>actual.err &&
	cat >expect <<-EOF &&
	fatal: invalid value for ${SQ}fetch.output${SQ}: ${SQ}${SQ}
	EOF
	test_cmp expect actual.err &&

	test_must_fail git -C clone -c fetch.output=garbage fetch origin 2>actual.err &&
	cat >expect <<-EOF &&
	fatal: invalid value for ${SQ}fetch.output${SQ}: ${SQ}garbage${SQ}
	EOF
	test_cmp expect actual.err
'

test_expect_success 'fetch aligned output' '
	git clone . full-output &&
	test_commit looooooooooooong-tag &&
	(
		cd full-output &&
		git -c fetch.output=full fetch origin >actual 2>&1 &&
		grep -e "->" actual | cut -c 22- >../actual
	) &&
	cat >expect <<-\EOF &&
	main                 -> origin/main
	looooooooooooong-tag -> looooooooooooong-tag
	EOF
	test_cmp expect actual
'

test_expect_success 'fetch compact output' '
	git clone . compact &&
	test_commit extraaa &&
	(
		cd compact &&
		git -c fetch.output=compact fetch origin >actual 2>&1 &&
		grep -e "->" actual | cut -c 22- >../actual
	) &&
	cat >expect <<-\EOF &&
	main       -> origin/*
	extraaa    -> *
	EOF
	test_cmp expect actual
'

test_expect_success 'fetch output with HEAD' '
	test_when_finished "rm -rf head" &&
	git clone . head &&

	git -C head fetch --dry-run origin HEAD >actual.out 2>actual.err &&
	cat >expect <<-EOF &&
	From $(test-tool path-utils real_path .)/.
	 * branch            HEAD       -> FETCH_HEAD
	EOF
	test_must_be_empty actual.out &&
	test_cmp expect actual.err &&

	git -C head fetch origin HEAD >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_cmp expect actual.err &&

	git -C head fetch --dry-run origin HEAD:foo >actual.out 2>actual.err &&
	cat >expect <<-EOF &&
	From $(test-tool path-utils real_path .)/.
	 * [new ref]         HEAD       -> foo
	EOF
	test_must_be_empty actual.out &&
	test_cmp expect actual.err &&

	git -C head fetch origin HEAD:foo >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_cmp expect actual.err
'

test_expect_success 'fetch output with object ID' '
	test_when_finished "rm -rf object-id" &&
	git clone . object-id &&
	commit=$(git rev-parse HEAD) &&

	git -C object-id fetch --dry-run origin $commit:object-id >actual.out 2>actual.err &&
	cat >expect <<-EOF &&
	From $(test-tool path-utils real_path .)/.
	 * [new ref]         $commit -> object-id
	EOF
	test_must_be_empty actual.out &&
	test_cmp expect actual.err &&

	git -C object-id fetch origin $commit:object-id >actual.out 2>actual.err &&
	test_must_be_empty actual.out &&
	test_cmp expect actual.err
'

test_expect_success '--no-show-forced-updates' '
	mkdir forced-updates &&
	(
		cd forced-updates &&
		git init &&
		test_commit 1 &&
		test_commit 2
	) &&
	git clone forced-updates forced-update-clone &&
	git clone forced-updates no-forced-update-clone &&
	git -C forced-updates reset --hard HEAD~1 &&
	(
		cd forced-update-clone &&
		git fetch --show-forced-updates origin 2>output &&
		test_i18ngrep "(forced update)" output
	) &&
	(
		cd no-forced-update-clone &&
		git fetch --no-show-forced-updates origin 2>output &&
		test_i18ngrep ! "(forced update)" output
	)
'

test_done
