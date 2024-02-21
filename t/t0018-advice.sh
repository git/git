#!/bin/sh

test_description='Test advise_if_enabled functionality'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'advice should be printed when config variable is unset' '
	cat >expect <<-\EOF &&
	hint: This is a piece of advice
	hint: Disable this message with "git config advice.nestedTag false"
	EOF
	test-tool advise "This is a piece of advice" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'advice should be printed when config variable is set to true' '
	cat >expect <<-\EOF &&
	hint: This is a piece of advice
	EOF
	test_config advice.nestedTag true &&
	test-tool advise "This is a piece of advice" 2>actual &&
	test_cmp expect actual
'

test_expect_success 'advice should not be printed when config variable is set to false' '
	test_config advice.nestedTag false &&
	test-tool advise "This is a piece of advice" 2>actual &&
	test_must_be_empty actual
'

test_done
