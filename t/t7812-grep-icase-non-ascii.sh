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

test_expect_success GETTEXT_LOCALE,PCRE 'log --author with an ascii pattern on UTF-8 data' '
	cat >expected <<-\EOF &&
	Author: <BOLD;RED>À Ú Thor<RESET> <author@example.com>
	EOF
	test_write_lines "forth" >file4 &&
	git add file4 &&
	git commit --author="À Ú Thor <author@example.com>" -m sécond &&
	git log -1 --color=always --perl-regexp --author=".*Thor" >log &&
	grep Author log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,PCRE 'log --committer with an ascii pattern on ISO-8859-1 data' '
	cat >expected <<-\EOF &&
	Commit:     Ç<BOLD;RED> O Mîtter <committer@example.com><RESET>
	EOF
	test_write_lines "fifth" >file5 &&
	git add file5 &&
	GIT_COMMITTER_NAME="Ç O Mîtter" &&
	GIT_COMMITTER_EMAIL="committer@example.com" &&
	git -c i18n.commitEncoding=latin1 commit -m thïrd &&
	git -c i18n.logOutputEncoding=latin1 log -1 --pretty=fuller --color=always --perl-regexp --committer=" O.*" >log &&
	grep Commit: log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,PCRE 'log --grep with an ascii pattern on UTF-8 data' '
	cat >expected <<-\EOF &&
	    sé<BOLD;RED>con<RESET>d
	EOF
	git log -1 --color=always --perl-regexp --grep="con" >log &&
	grep con log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,PCRE 'log --grep with an ascii pattern on ISO-8859-1 data' '
	cat >expected <<-\EOF &&
	    <BOLD;RED>thïrd<RESET>
	EOF
	git -c i18n.logOutputEncoding=latin1 log -1 --color=always --perl-regexp --grep="th.*rd" >log &&
	grep "th.*rd" log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: setup invalid UTF-8 data' '
	printf "\\200\\n" >invalid-0x80 &&
	echo "ævar" >expected &&
	cat expected >>invalid-0x80 &&
	git add invalid-0x80 &&

	# Test for PCRE2_MATCH_INVALID_UTF bug
	# https://bugs.exim.org/show_bug.cgi?id=2642
	printf "\\345Aæ\\n" >invalid-0xe5 &&
	git add invalid-0xe5
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: grep ASCII from invalid UTF-8 data' '
	git grep -h "var" invalid-0x80 >actual &&
	test_cmp expected actual &&
	git grep -h "(*NO_JIT)var" invalid-0x80 >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: grep ASCII from invalid UTF-8 data (PCRE2 bug #2642)' '
	git grep -h "Aæ" invalid-0xe5 >actual &&
	test_cmp invalid-0xe5 actual &&
	git grep -h "(*NO_JIT)Aæ" invalid-0xe5 >actual &&
	test_cmp invalid-0xe5 actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: grep non-ASCII from invalid UTF-8 data' '
	git grep -h "æ" invalid-0x80 >actual &&
	test_cmp expected actual &&
	git grep -h "(*NO_JIT)æ" invalid-0x80 >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: grep non-ASCII from invalid UTF-8 data (PCRE2 bug #2642)' '
	git grep -h "Aæ" invalid-0xe5 >actual &&
	test_cmp invalid-0xe5 actual &&
	git grep -h "(*NO_JIT)Aæ" invalid-0xe5 >actual &&
	test_cmp invalid-0xe5 actual
'

test_lazy_prereq PCRE2_MATCH_INVALID_UTF '
	test-tool pcre2-config has-PCRE2_MATCH_INVALID_UTF
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2 'PCRE v2: grep non-ASCII from invalid UTF-8 data with -i' '
	test_might_fail git grep -hi "Æ" invalid-0x80 >actual &&
	test_might_fail git grep -hi "(*NO_JIT)Æ" invalid-0x80 >actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2,PCRE2_MATCH_INVALID_UTF 'PCRE v2: grep non-ASCII from invalid UTF-8 data with -i' '
	git grep -hi "Æ" invalid-0x80 >actual &&
	test_cmp expected actual &&
	git grep -hi "(*NO_JIT)Æ" invalid-0x80 >actual &&
	test_cmp expected actual
'

test_expect_success GETTEXT_LOCALE,LIBPCRE2,PCRE2_MATCH_INVALID_UTF 'PCRE v2: grep non-ASCII from invalid UTF-8 data with -i (PCRE2 bug #2642)' '
	git grep -hi "Æ" invalid-0xe5 >actual &&
	test_cmp invalid-0xe5 actual &&
	git grep -hi "(*NO_JIT)Æ" invalid-0xe5 >actual &&
	test_cmp invalid-0xe5 actual &&

	# Only the case of grepping the ASCII part in a way that
	# relies on -i fails
	git grep -hi "aÆ" invalid-0xe5 >actual &&
	test_cmp invalid-0xe5 actual &&
	git grep -hi "(*NO_JIT)aÆ" invalid-0xe5 >actual &&
	test_cmp invalid-0xe5 actual
'

test_done
