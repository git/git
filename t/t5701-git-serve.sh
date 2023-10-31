#!/bin/sh

test_description='test protocol v2 server commands'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'test capability advertisement' '
	test_oid_cache <<-EOF &&
	wrong_algo sha1:sha256
	wrong_algo sha256:sha1
	EOF
	cat >expect.base <<-EOF &&
	version 2
	agent=git/$(git version | cut -d" " -f3)
	ls-refs=unborn
	fetch=shallow wait-for-done
	server-option
	object-format=$(test_oid algo)
	object-info
	EOF
	cat >expect.trailer <<-EOF &&
	0000
	EOF
	cat expect.base expect.trailer >expect &&

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
	test_grep "unknown capability" err
'

test_expect_success 'request with no command' '
	test-tool pkt-line pack >in <<-EOF &&
	agent=git/test
	object-format=$(test_oid algo)
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	test_grep "no command requested" err
'

test_expect_success 'request invalid command' '
	test-tool pkt-line pack >in <<-EOF &&
	command=foo
	object-format=$(test_oid algo)
	agent=git/test
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	test_grep "invalid command" err
'

test_expect_success 'request capability as command' '
	test-tool pkt-line pack >in <<-EOF &&
	command=agent
	object-format=$(test_oid algo)
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	grep invalid.command.*agent err
'

test_expect_success 'request command as capability' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	object-format=$(test_oid algo)
	fetch
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	grep unknown.capability err
'

test_expect_success 'requested command is command=value' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs=whatever
	object-format=$(test_oid algo)
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	grep invalid.command.*ls-refs=whatever err
'

test_expect_success 'wrong object-format' '
	test-tool pkt-line pack >in <<-EOF &&
	command=fetch
	agent=git/test
	object-format=$(test_oid wrong_algo)
	0000
	EOF
	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	test_grep "mismatched object format" err
'

# Test the basics of ls-refs
#
test_expect_success 'setup some refs and tags' '
	test_commit one &&
	git branch dev main &&
	test_commit two &&
	git symbolic-ref refs/heads/release refs/heads/main &&
	git tag -a -m "annotated tag" annotated-tag
'

test_expect_success 'basics of ls-refs' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	object-format=$(test_oid algo)
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse HEAD) HEAD
	$(git rev-parse refs/heads/dev) refs/heads/dev
	$(git rev-parse refs/heads/main) refs/heads/main
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

test_expect_success 'ls-refs complains about unknown options' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	object-format=$(test_oid algo)
	0001
	no-such-arg
	0000
	EOF

	test_must_fail test-tool serve-v2 --stateless-rpc 2>err <in &&
	grep unexpected.line.*no-such-arg err
'

test_expect_success 'basic ref-prefixes' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	object-format=$(test_oid algo)
	0001
	ref-prefix refs/heads/main
	ref-prefix refs/tags/one
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/main) refs/heads/main
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
	object-format=$(test_oid algo)
	0001
	ref-prefix refs/heads/
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/dev) refs/heads/dev
	$(git rev-parse refs/heads/main) refs/heads/main
	$(git rev-parse refs/heads/release) refs/heads/release
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'ignore very large set of prefixes' '
	# generate a large number of ref-prefixes that we expect
	# to match nothing; the value here exceeds TOO_MANY_PREFIXES
	# from ls-refs.c.
	{
		echo command=ls-refs &&
		echo object-format=$(test_oid algo) &&
		echo 0001 &&
		perl -le "print \"ref-prefix refs/heads/\$_\" for (1..65536)" &&
		echo 0000
	} |
	test-tool pkt-line pack >in &&

	# and then confirm that we see unmatched prefixes anyway (i.e.,
	# that the prefix was not applied).
	cat >expect <<-EOF &&
	$(git rev-parse HEAD) HEAD
	$(git rev-parse refs/heads/dev) refs/heads/dev
	$(git rev-parse refs/heads/main) refs/heads/main
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

test_expect_success 'peel parameter' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	object-format=$(test_oid algo)
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
	object-format=$(test_oid algo)
	0001
	symrefs
	ref-prefix refs/heads/
	0000
	EOF

	cat >expect <<-EOF &&
	$(git rev-parse refs/heads/dev) refs/heads/dev
	$(git rev-parse refs/heads/main) refs/heads/main
	$(git rev-parse refs/heads/release) refs/heads/release symref-target:refs/heads/main
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'sending server-options' '
	test-tool pkt-line pack >in <<-EOF &&
	command=ls-refs
	object-format=$(test_oid algo)
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
	object-format=$(test_oid algo)
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

# Test the basics of object-info
#
test_expect_success 'basics of object-info' '
	test-tool pkt-line pack >in <<-EOF &&
	command=object-info
	object-format=$(test_oid algo)
	0001
	size
	oid $(git rev-parse two:two.t)
	oid $(git rev-parse two:two.t)
	0000
	EOF

	cat >expect <<-EOF &&
	size
	$(git rev-parse two:two.t) $(wc -c <two.t | xargs)
	$(git rev-parse two:two.t) $(wc -c <two.t | xargs)
	0000
	EOF

	test-tool serve-v2 --stateless-rpc <in >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'test capability advertisement with uploadpack.advertiseBundleURIs' '
	test_config uploadpack.advertiseBundleURIs true &&

	cat >expect.extra <<-EOF &&
	bundle-uri
	EOF
	cat expect.base \
	    expect.extra \
	    expect.trailer >expect &&

	GIT_TEST_SIDEBAND_ALL=0 test-tool serve-v2 \
		--advertise-capabilities >out &&
	test-tool pkt-line unpack <out >actual &&
	test_cmp expect actual
'

test_expect_success 'basics of bundle-uri: dies if not enabled' '
	test-tool pkt-line pack >in <<-EOF &&
	command=bundle-uri
	0000
	EOF

	cat >err.expect <<-\EOF &&
	fatal: invalid command '"'"'bundle-uri'"'"'
	EOF

	cat >expect <<-\EOF &&
	ERR serve: invalid command '"'"'bundle-uri'"'"'
	EOF

	test_must_fail test-tool serve-v2 --stateless-rpc <in >out 2>err.actual &&
	test_cmp err.expect err.actual &&
	test_must_be_empty out
'

test_done
