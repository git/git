#!/bin/sh

test_description='Test "contains" argument behavior'

. ./test-lib.sh

test_expect_success 'setup ' '
	git init . &&
	echo "this is a test" >file &&
	git add -A &&
	git commit -am "tag test" &&
	git tag "v1.0" &&
	git tag "v1.1"
'

test_expect_success 'tag --contains <existent_tag>' '
	git tag --contains "v1.0" >actual &&
	grep "v1.0" actual &&
	grep "v1.1" actual
'

test_expect_success 'tag --contains <inexistent_tag>' '
	test_must_fail git tag --contains "notag" 2>actual &&
	test_i18ngrep "error" actual
'

test_expect_success 'tag --no-contains <existent_tag>' '
	git tag --no-contains "v1.1" >actual &&
	test_line_count = 0 actual
'

test_expect_success 'tag --no-contains <inexistent_tag>' '
	test_must_fail git tag --no-contains "notag" 2>actual &&
	test_i18ngrep "error" actual
'

test_expect_success 'tag usage error' '
	test_must_fail git tag --noopt 2>actual &&
	test_i18ngrep "usage" actual
'

test_expect_success 'branch --contains <existent_commit>' '
	git branch --contains "master" >actual &&
	test_i18ngrep "master" actual
'

test_expect_success 'branch --contains <inexistent_commit>' '
	test_must_fail git branch --no-contains "nocommit" 2>actual &&
	test_i18ngrep "error" actual
'

test_expect_success 'branch --no-contains <existent_commit>' '
	git branch --no-contains "master" >actual &&
	test_line_count = 0 actual
'

test_expect_success 'branch --no-contains <inexistent_commit>' '
	test_must_fail git branch --no-contains "nocommit" 2>actual &&
	test_i18ngrep "error" actual
'

test_expect_success 'branch usage error' '
	test_must_fail git branch --noopt 2>actual &&
	test_i18ngrep "usage" actual
'

test_expect_success 'for-each-ref --contains <existent_object>' '
	git for-each-ref --contains "master" >actual &&
	test_line_count = 3 actual
'

test_expect_success 'for-each-ref --contains <inexistent_object>' '
	test_must_fail git for-each-ref --no-contains "noobject" 2>actual &&
	test_i18ngrep "error" actual
'

test_expect_success 'for-each-ref --no-contains <existent_object>' '
	git for-each-ref --no-contains "master" >actual &&
	test_line_count = 0 actual
'

test_expect_success 'for-each-ref --no-contains <inexistent_object>' '
	test_must_fail git for-each-ref --no-contains "noobject" 2>actual &&
	test_i18ngrep "error" actual
'

test_expect_success 'for-each-ref usage error' '
	test_must_fail git for-each-ref --noopt 2>actual &&
	test_i18ngrep "usage" actual
'

test_done
