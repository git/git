#!/bin/sh

test_description='tests for git clone -c key=value'
. ./test-lib.sh

test_expect_success 'clone -c sets config in cloned repo' '
	rm -rf child &&
	git clone -c core.foo=bar . child &&
	echo bar >expect &&
	git --git-dir=child/.git config core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'clone -c can set multi-keys' '
	rm -rf child &&
	git clone -c core.foo=bar -c core.foo=baz . child &&
	{ echo bar; echo baz; } >expect &&
	git --git-dir=child/.git config --get-all core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'clone -c without a value is boolean true' '
	rm -rf child &&
	git clone -c core.foo . child &&
	echo true >expect &&
	git --git-dir=child/.git config --bool core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'clone -c config is available during clone' '
	echo content >file &&
	git add file &&
	git commit -m one &&
	rm -rf child &&
	git clone -c core.autocrlf . child &&
	printf "content\\r\\n" >expect &&
	test_cmp expect child/file
'

test_done
