#!/bin/sh

test_description='grep icase on non-English locales'

. ./lib-gettext.sh

test_expect_success GETTEXT_LOCALE 'setup' '
	test_write_lines "TILRAUN: Halló Heimur!" >file &&
	git add file &&
	LC_ALL="$is_IS_locale" &&
	export LC_ALL
'

test_have_prereq GETTEXT_LOCALE &&
test-tool regex "HALLÓ" "Halló" ICASE &&
test_set_prereq REGEX_LOCALE

test_expect_success REGEX_LOCALE 'grep literal string, no -F' '
	git grep -i "TILRAUN: Halló Heimur!" &&
	git grep -i "TILRAUN: HALLÓ HEIMUR!"
'

test_expect_success GETTEXT_LOCALE,PCRE 'grep pcre utf-8 icase' '
	git grep --perl-regexp    "TILRAUN: H.lló Heimur!" &&
	git grep --perl-regexp -i "TILRAUN: H.lló Heimur!" &&
	git grep --perl-regexp -i "TILRAUN: H.LLÓ HEIMUR!"
'

test_expect_success GETTEXT_LOCALE,PCRE 'grep pcre utf-8 string with "+"' '
	test_write_lines "TILRAUN: Hallóó Heimur!" >file2 &&
	git add file2 &&
	git grep -l --perl-regexp "TILRAUN: H.lló+ Heimur!" >actual &&
	echo file >expected &&
	echo file2 >>expected &&
	test_cmp expected actual
'

test_expect_success REGEX_LOCALE 'grep literal string, with -F' '
	git grep -i -F "TILRAUN: Halló Heimur!" &&
	git grep -i -F "TILRAUN: HALLÓ HEIMUR!"
'

test_expect_success REGEX_LOCALE 'grep string with regex, with -F' '
	test_write_lines "TILRAUN: Halló Heimur [abc]!" >file3 &&
	git add file3 &&
	git grep -i -F "TILRAUN: Halló Heimur [abc]!" file3
'

test_expect_success REGEX_LOCALE 'pickaxe -i on non-ascii' '
	git commit -m first &&
	git log --format=%f -i -S"TILRAUN: HALLÓ HEIMUR!" >actual &&
	echo first >expected &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: setup invalid UTF-8 data' '
	printf "\\200\\n" >invalid-0x80 &&
	echo "ævar" >expected &&
	cat expected >>invalid-0x80 &&
	git add invalid-0x80
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: grep ASCII from invalid UTF-8 data' '
	git grep -h "var" invalid-0x80 >actual &&
	test_cmp expected actual &&
	git grep -h "(*NO_JIT)var" invalid-0x80 >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: grep non-ASCII from invalid UTF-8 data' '
	git grep -h "æ" invalid-0x80 >actual &&
	test_cmp expected actual &&
	git grep -h "(*NO_JIT)æ" invalid-0x80 >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: grep non-ASCII from invalid UTF-8 data with -i' '
	test_might_fail git grep -hi "Æ" invalid-0x80 >actual &&
	if test -s actual
	then
	    test_cmp expected actual
	fi &&
	test_must_fail git grep -hi "(*NO_JIT)Æ" invalid-0x80 >actual &&
	! test_cmp expected actual
'

test_done
