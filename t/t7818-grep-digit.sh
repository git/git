#!/bin/sh

test_description='git grep -P with digits'

TEST_PASSES_SANITIZE_LEAK=true
. ./lib-gettext.sh

test_expect_success 'setup' '
	echo 2023 >ascii &&
	printf "\357\274\222\357\274\220\357\274\222\357\274\223\n" >fullwidth &&
	printf "\331\241\331\244\331\244\331\245\n" >multibyte &&
	git add . &&
	git commit -m. &&
	LC_ALL="$is_IS_locale" &&
	export LC_ALL
'

test_expect_success PCRE 'grep -P "\d"' '
	echo "ascii:2023" >expected &&
	git grep -P "\d{2}[[:digit:]]{2}" >actual &&
	test_cmp expected actual
'

test_expect_success PCRE 'git -c grep.perl.digit' '
	test_config grep.perl.digit true &&
	git grep -P "\d{2}[[:digit:]]{2}" >actual &&
	grep fullwidth actual &&
	grep multibyte actual &&
	test_line_count = 3 actual
'

test_done
