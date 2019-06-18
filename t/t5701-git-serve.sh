#!/bin/sh

test_description='test protocol v2 server commands'

. ./test-lib.sh

test_expect_success 'test capability advertisement' '
	cat >expect <<-EOF &&
	version 2
	agent=git/$(git version | cut -d" " -f3)
	ls-refs
	fetch=shallow
	server-option
	0000
	EOF

	GIT_TEST_SIDEBAND_ALL=0 test-tool serve-v2 \
		--advertise-capabilities >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'stateless-rpc flag does not list capabilities' '
	# Empty request
	test-tool pkt-line pack >in <<-EOF &&
	0000
	EOF
	test-tool serve-v2 --stateless-rpc >out <in &&
	test_must_be_empty out &&

	# EOF
	test-tool serve-v2 --stateless-rpc >out &&
	test_must_be_empty out
'

test_expect_success 'request invalid capability' '
	test-tool pkt-line pack >in <<-EOF &&
	foobar
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	test_i18ngrep "unknown capability" err
'

test_expect_success 'request with no command' '
	test-tool pkt-line pack >in <<-EOF &&
	agent=git/test
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	test_i18ngrep "no command requested" err
'

test_expect_success 'request invalid command' '
	test-tool pkt-line pack >in <<-EOF &&
	command=foo
	agent=git/test
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	test_i18ngrep "invalid command" err
'

# Test the basics of ls-refs
#
test_expect_success 'setup some refs and tags' '
	test_commit one &&
	git branch dev master &&
	test_commit two &&
	git symbolic-ref refs/heads/release refs/heads/master &&
	git tag -a -m "annotated tag" annotated-tag
'

test_expect_success 'basics of ls-refs' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse HEAD) HEAD
	$(git rev-parse refs/heads/dev) refs/heads/dev
	$(git rev-parse refs/heads/master) refs/heads/master
	$(git rev-parse refs/heads/release) refs/heads/release
	$(git rev-parse refs/tags/annotated-tag) refs/tags/annotated-tag
	$(git rev-parse refs/tags/one) refs/tags/one
	$(git rev-parse refs/tags/two) refs/tags/two
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'basic ref-prefixes' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	0001
	ref-prefix refs/heads/master
	ref-prefix refs/tags/one
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/master) refs/heads/master
	$(git rev-parse refs/tags/one) refs/tags/one
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'refs/heads prefix' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	0001
	ref-prefix refs/heads/
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/dev) refs/heads/dev
	$(git rev-parse refs/heads/master) refs/heads/master
	$(git rev-parse refs/heads/release) refs/heads/release
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'peel parameter' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	0001
	peel
	ref-prefix refs/tags/
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/tags/annotated-tag) refs/tags/annotated-tag peeled:$(git rev-parse refs/tags/annotated-tag^{})
	$(git rev-parse refs/tags/one) refs/tags/one
	$(git rev-parse refs/tags/two) refs/tags/two
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'symrefs parameter' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	0001
	symrefs
	ref-prefix refs/heads/
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/dev) refs/heads/dev
	$(git rev-parse refs/heads/master) refs/heads/master
	$(git rev-parse refs/heads/release) refs/heads/release symref-target:refs/heads/master
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'sending server-options' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	server-option=hello
	server-option=world
	0001
	ref-prefix HEAD
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse HEAD) HEAD
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'unexpected lines are not allowed in fetch request' '
	git init server &&

	test-tool pkt-line pack >in <<-EOF &&
	command=fetch
	0001
	this-is-not-a-command
	0000
	EOF

	(
		cd server &&
		test_must_fail test-tool serve-v2 --stateless-rpc
	) <in >/dev/null 2>err &&
	grep "unexpected line: .this-is-not-a-command." err
'

test_done
