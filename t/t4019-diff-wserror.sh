#!/bin/sh

test_description='diff whitespace error detection'

. ./test-lib.sh

test_expect_success setup '

	git config diff.color.whitespace "blue reverse" &&
	>F &&
	git add F &&
	echo "         Eight SP indent" >>F &&
	echo " 	HT and SP indent" >>F &&
	echo "With trailing SP " >>F &&
	echo "Carriage ReturnQ" | tr Q "\015" >>F &&
	echo "No problem" >>F &&
	echo >>F

'

blue_grep='7;34m' ;# ESC [ 7 ; 3 4 m

printf "\033[%s" "$blue_grep" >check-grep
if (grep "$blue_grep" <check-grep | grep "$blue_grep") >/dev/null 2>&1
then
	grep_a=grep
elif (grep -a "$blue_grep" <check-grep | grep -a "$blue_grep") >/dev/null 2>&1
then
	grep_a='grep -a'
else
	grep_a=grep ;# expected to fail...
fi
rm -f check-grep

prepare_output () {
	git diff --color >output
	$grep_a "$blue_grep" output >error
	$grep_a -v "$blue_grep" output >normal
	return 0
}

test_expect_success default '

	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT error >/dev/null &&
	grep With error >/dev/null &&
	grep Return error >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'default (attribute)' '

	test_might_fail git config --unset core.whitespace &&
	echo "F whitespace" >.gitattributes &&
	prepare_output &&

	grep Eight error >/dev/null &&
	grep HT error >/dev/null &&
	grep With error >/dev/null &&
	grep Return error >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'default, tabwidth=10 (attribute)' '

	git config core.whitespace "tabwidth=10" &&
	echo "F whitespace" >.gitattributes &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT error >/dev/null &&
	grep With error >/dev/null &&
	grep Return error >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'no check (attribute)' '

	test_might_fail git config --unset core.whitespace &&
	echo "F -whitespace" >.gitattributes &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT normal >/dev/null &&
	grep With normal >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'no check, tabwidth=10 (attribute), must be irrelevant' '

	git config core.whitespace "tabwidth=10" &&
	echo "F -whitespace" >.gitattributes &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT normal >/dev/null &&
	grep With normal >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'without -trail' '

	rm -f .gitattributes &&
	git config core.whitespace -trail &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT error >/dev/null &&
	grep With normal >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'without -trail (attribute)' '

	test_might_fail git config --unset core.whitespace &&
	echo "F whitespace=-trail" >.gitattributes &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT error >/dev/null &&
	grep With normal >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'without -space' '

	rm -f .gitattributes &&
	git config core.whitespace -space &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT normal >/dev/null &&
	grep With error >/dev/null &&
	grep Return error >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'without -space (attribute)' '

	test_might_fail git config --unset core.whitespace &&
	echo "F whitespace=-space" >.gitattributes &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT normal >/dev/null &&
	grep With error >/dev/null &&
	grep Return error >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'with indent-non-tab only' '

	rm -f .gitattributes &&
	git config core.whitespace indent,-trailing,-space &&
	prepare_output &&

	grep Eight error >/dev/null &&
	grep HT normal >/dev/null &&
	grep With normal >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'with indent-non-tab only (attribute)' '

	test_might_fail git config --unset core.whitespace &&
	echo "F whitespace=indent,-trailing,-space" >.gitattributes &&
	prepare_output &&

	grep Eight error >/dev/null &&
	grep HT normal >/dev/null &&
	grep With normal >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'with indent-non-tab only, tabwidth=10' '

	rm -f .gitattributes &&
	git config core.whitespace indent,tabwidth=10,-trailing,-space &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT normal >/dev/null &&
	grep With normal >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'with indent-non-tab only, tabwidth=10 (attribute)' '

	test_might_fail git config --unset core.whitespace &&
	echo "F whitespace=indent,-trailing,-space,tabwidth=10" >.gitattributes &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT normal >/dev/null &&
	grep With normal >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'with cr-at-eol' '

	rm -f .gitattributes &&
	git config core.whitespace cr-at-eol &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT error >/dev/null &&
	grep With error >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'with cr-at-eol (attribute)' '

	test_might_fail git config --unset core.whitespace &&
	echo "F whitespace=trailing,cr-at-eol" >.gitattributes &&
	prepare_output &&

	grep Eight normal >/dev/null &&
	grep HT error >/dev/null &&
	grep With error >/dev/null &&
	grep Return normal >/dev/null &&
	grep No normal >/dev/null

'

test_expect_success 'trailing empty lines (1)' '

	rm -f .gitattributes &&
	test_must_fail git diff --check >output &&
	grep "new blank line at" output &&
	grep "trailing whitespace" output

'

test_expect_success 'trailing empty lines (2)' '

	echo "F -whitespace" >.gitattributes &&
	git diff --check >output &&
	test_must_be_empty output

'

test_expect_success 'checkdiff shows correct line number for trailing blank lines' '

	printf "a\nb\n" > G &&
	git add G &&
	printf "x\nx\nx\na\nb\nc\n\n" > G &&
	[ "$(git diff --check -- G)" = "G:7: new blank line at EOF." ]

'

test_expect_success 'do not color trailing cr in context' '
	test_might_fail git config --unset core.whitespace &&
	rm -f .gitattributes &&
	echo AAAQ | tr Q "\015" >G &&
	git add G &&
	echo BBBQ | tr Q "\015" >>G &&
	git diff --color G | tr "\015" Q >output &&
	grep "BBB.*${blue_grep}Q" output &&
	grep "AAA.*\[mQ" output

'

test_expect_success 'color new trailing blank lines' '
	test_write_lines a b "" "" >x &&
	git add x &&
	test_write_lines a "" "" "" c "" "" "" "" >x &&
	git diff --color x >output &&
	cnt=$($grep_a "${blue_grep}" output | wc -l) &&
	test $cnt = 2
'

test_done
