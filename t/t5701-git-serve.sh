#!/bin/sh

test_description='test git-serve and server commands'

. ./test-lib.sh

test_expect_success 'test capability advertisement' '
	cat >expect <<-EOF &&
	version 2
	agent=git/$(git version | cut -d" " -f3)
	0000
	EOF

	git serve --advertise-capabilities >out &&
	test-pkt-line unpack <out >actual &&
	test_cmp actual expect
'

test_expect_success 'stateless-rpc flag does not list capabilities' '
	# Empty request
	test-pkt-line pack >in <<-EOF &&
	0000
	EOF
	git serve --stateless-rpc >out <in &&
	test_must_be_empty out &&

	# EOF
	git serve --stateless-rpc >out &&
	test_must_be_empty out
'

test_expect_success 'request invalid capability' '
	test-pkt-line pack >in <<-EOF &&
	foobar
	0000
	EOF
	test_must_fail git serve --stateless-rpc 2>err <in &&
	test_i18ngrep "unknown capability" err
'

test_expect_success 'request with no command' '
	test-pkt-line pack >in <<-EOF &&
	agent=git/test
	0000
	EOF
	test_must_fail git serve --stateless-rpc 2>err <in &&
	test_i18ngrep "no command requested" err
'

test_expect_success 'request invalid command' '
	test-pkt-line pack >in <<-EOF &&
	command=foo
	agent=git/test
	0000
	EOF
	test_must_fail git serve --stateless-rpc 2>err <in &&
	test_i18ngrep "invalid command" err
'

test_done
