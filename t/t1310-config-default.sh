#!/bin/sh

test_description='Test git config in different settings (with --default)'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'uses --default when entry missing' '
	echo quux >expect &&
	git config -f config --default=quux core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'does not use --default when entry present' '
	echo bar >expect &&
	git -c core.foo=bar config --default=baz core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'canonicalizes --default with appropriate type' '
	echo true >expect &&
	git config -f config --default=yes --bool core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'dies when --default cannot be parsed' '
	test_must_fail git config -f config --type=expiry-date --default=x --get \
		not.a.section 2>error &&
	test_i18ngrep "failed to format default config value" error
'

test_expect_success 'does not allow --default without --get' '
	test_must_fail git config --default=quux --unset a.section >output 2>&1 &&
	test_i18ngrep "\-\-default is only applicable to" output
'

test_done
