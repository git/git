#!/bin/sh

test_description='ignored hook warning'

. ./test-lib.sh

test_expect_success setup '
	test_hook --setup pre-cummit <<-\EOF
	exit 0
	EOF
'

test_expect_success 'no warning if hook is not ignored' '
	git cummit --allow-empty -m "more" 2>message &&
	test_i18ngrep ! -e "hook was ignored" message
'

test_expect_success POSIXPERM 'warning if hook is ignored' '
	test_hook --disable pre-cummit &&
	git cummit --allow-empty -m "even more" 2>message &&
	test_i18ngrep -e "hook was ignored" message
'

test_expect_success POSIXPERM 'no warning if advice.ignoredHook set to false' '
	test_config advice.ignoredHook false &&
	test_hook --disable pre-cummit &&
	git cummit --allow-empty -m "even more" 2>message &&
	test_i18ngrep ! -e "hook was ignored" message
'

test_expect_success 'no warning if unset advice.ignoredHook and hook removed' '
	test_hook --remove pre-cummit &&
	test_unconfig advice.ignoredHook &&
	git cummit --allow-empty -m "even more" 2>message &&
	test_i18ngrep ! -e "hook was ignored" message
'

test_done
