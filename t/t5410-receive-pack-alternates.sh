#!/bin/sh

test_description='git receive-pack with alternate ref filtering'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit base &&
	git clone -s --bare . fork &&
	git checkout -b public/branch main &&
	test_commit public &&
	git checkout -b private/branch main &&
	test_commit private
'

extract_haves () {
	depacketize | perl -lne '/^(\S+) \.have/ and print $1'
}

test_expect_success 'with core.alternateRefsCommand' '
	write_script fork/alternate-refs <<-\EOF &&
		git --git-dir="$1" for-each-ref \
			--format="%(objectname)" \
			refs/heads/public/
	EOF
	test_config -C fork core.alternateRefsCommand ./alternate-refs &&
	git rev-parse public/branch >expect &&
	printf "0000" | git receive-pack fork >actual &&
	extract_haves <actual >actual.haves &&
	test_cmp expect actual.haves
'

test_expect_success 'with core.alternateRefsPrefixes' '
	test_config -C fork core.alternateRefsPrefixes "refs/heads/private" &&
	git rev-parse private/branch >expect &&
	printf "0000" | git receive-pack fork >actual &&
	extract_haves <actual >actual.haves &&
	test_cmp expect actual.haves
'

test_done
