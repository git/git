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

test_expect_success 'with core.alternateRefsCommand' '
	write_script fork/alternate-refs <<-\EOF &&
		git --git-dir="$1" for-each-ref \
			--format="%(objectname)" \
			refs/heads/public/
	EOF
	test_config -C fork core.alternateRefsCommand ./alternate-refs &&

	test-tool pkt-line pack >in <<-\EOF &&
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse main) refs/heads/main
	$(git rev-parse base) refs/tags/base
	$(git rev-parse public) .have
	0000
	EOF

	git receive-pack fork >out <in &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'with core.alternateRefsPrefixes' '
	test_config -C fork core.alternateRefsPrefixes "refs/heads/private" &&

	test-tool pkt-line pack >in <<-\EOF &&
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse main) refs/heads/main
	$(git rev-parse base) refs/tags/base
	$(git rev-parse private) .have
	0000
	EOF

	git receive-pack fork >out <in &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_done
