#!/bin/sh

test_description='Test responses to violations of the network protocol. In most
of these cases it will generally be acceptable for one side to break off
communications if the other side says something unexpected. We are mostly
making sure that we do not segfault or otherwise behave badly.'
. ./test-lib.sh

test_expect_success 'extra delim packet in v2 ls-refs args' '
	# protocol expects 0000 flush after the 0001
	test-tool pkt-line pack >input <<-EOF &&
	command=ls-refs
	object-format=$(test_oid algo)
	0001
	0001
	EOF

	cat >err.expect <<-\EOF &&
	fatal: expected flush after ls-refs arguments
	EOF
	test_must_fail env GIT_PROTOCOL=version=2 \
		git upload-pack . <input 2>err.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'extra delim packet in v2 fetch args' '
	# protocol expects 0000 flush after the 0001
	test-tool pkt-line pack >input <<-EOF &&
	command=fetch
	object-format=$(test_oid algo)
	0001
	0001
	EOF

	cat >err.expect <<-\EOF &&
	fatal: expected flush after fetch arguments
	EOF
	test_must_fail env GIT_PROTOCOL=version=2 \
		git upload-pack . <input 2>err.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'extra delim packet in v2 object-info args' '
	# protocol expects 0000 flush after the 0001
	test-tool pkt-line pack >input <<-EOF &&
	command=object-info
	object-format=$(test_oid algo)
	0001
	0001
	EOF

	cat >err.expect <<-\EOF &&
	fatal: object-info: expected flush after arguments
	EOF
	test_must_fail env GIT_PROTOCOL=version=2 \
		git upload-pack . <input 2>err.actual &&
	test_cmp err.expect err.actual
'

test_done
