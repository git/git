#!/bin/sh
#
# Copyright (c) 2007 Carlos Rica
#

test_description='git stripspace'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

t40='A quick brown fox jumps over the lazy do'
s40='                                        '
sss="$s40$s40$s40$s40$s40$s40$s40$s40$s40$s40" # 400
ttt="$t40$t40$t40$t40$t40$t40$t40$t40$t40$t40" # 400

printf_git_stripspace () {
    printf "$1" | git stripspace
}

test_expect_success 'long lines without spaces should be unchanged' '
	echo "$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual &&

	echo "$ttt$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual &&

	echo "$ttt$ttt$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual &&

	echo "$ttt$ttt$ttt$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual
'

test_expect_success 'lines with spaces at the beginning should be unchanged' '
	echo "$sss$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual &&

	echo "$sss$sss$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual &&

	echo "$sss$sss$sss$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual
'

test_expect_success 'lines with intermediate spaces should be unchanged' '
	echo "$ttt$sss$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual &&

	echo "$ttt$sss$sss$ttt" >expect &&
	git stripspace <expect >actual &&
	test_cmp expect actual
'

test_expect_success 'consecutive blank lines should be unified' '
	printf "$ttt\n\n$ttt\n" > expect &&
	printf "$ttt\n\n\n\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt\n\n$ttt\n" > expect &&
	printf "$ttt$ttt\n\n\n\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt$ttt\n\n$ttt\n" > expect &&
	printf "$ttt$ttt$ttt\n\n\n\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$ttt\n" > expect &&
	printf "$ttt\n\n\n\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$ttt$ttt\n" > expect &&
	printf "$ttt\n\n\n\n\n$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$ttt$ttt$ttt\n" > expect &&
	printf "$ttt\n\n\n\n\n$ttt$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$ttt\n" > expect &&
	printf "$ttt\n\t\n \n\n  \t\t\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt\n\n$ttt\n" > expect &&
	printf "$ttt$ttt\n\t\n \n\n  \t\t\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt$ttt\n\n$ttt\n" > expect &&
	printf "$ttt$ttt$ttt\n\t\n \n\n  \t\t\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$ttt\n" > expect &&
	printf "$ttt\n\t\n \n\n  \t\t\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$ttt$ttt\n" > expect &&
	printf "$ttt\n\t\n \n\n  \t\t\n$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$ttt$ttt$ttt\n" > expect &&
	printf "$ttt\n\t\n \n\n  \t\t\n$ttt$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual
'

test_expect_success 'only consecutive blank lines should be completely removed' '
	printf "\n" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "\n\n\n" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "$sss\n$sss\n$sss\n" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "$sss$sss\n$sss\n\n" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "\n$sss\n$sss$sss\n" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "$sss$sss$sss$sss\n\n\n" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "\n$sss$sss$sss$sss\n\n" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "\n\n$sss$sss$sss$sss\n" | git stripspace >actual &&
	test_must_be_empty actual
'

test_expect_success 'consecutive blank lines at the beginning should be removed' '
	printf "$ttt\n" > expect &&
	printf "\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n" > expect &&
	printf "\n\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt\n" > expect &&
	printf "\n\n\n$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt$ttt\n" > expect &&
	printf "\n\n\n$ttt$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt$ttt$ttt\n" > expect &&
	printf "\n\n\n$ttt$ttt$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n" > expect &&

	printf "$sss\n$sss\n$sss\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "\n$sss\n$sss$sss\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$sss$sss\n$sss\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$sss$sss$sss\n\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "\n$sss$sss$sss\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "\n\n$sss$sss$sss\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual
'

test_expect_success 'consecutive blank lines at the end should be removed' '
	printf "$ttt\n" > expect &&
	printf "$ttt\n\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n" > expect &&
	printf "$ttt\n\n\n\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt\n" > expect &&
	printf "$ttt$ttt\n\n\n\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt$ttt\n" > expect &&
	printf "$ttt$ttt$ttt\n\n\n\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt$ttt$ttt\n" > expect &&
	printf "$ttt$ttt$ttt$ttt\n\n\n\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n" > expect &&

	printf "$ttt\n$sss\n$sss\n$sss\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$sss\n$sss$sss\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n$sss$sss\n$sss\n\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n$sss$sss$sss\n\n\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n$sss$sss$sss\n\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n\n\n$sss$sss$sss\n" | git stripspace >actual &&
	test_cmp expect actual
'

test_expect_success 'text without newline at end should end with newline' '
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt" &&
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$ttt" &&
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$ttt$ttt" &&
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$ttt$ttt$ttt"
'

# text plus spaces at the end:

test_expect_success 'text plus spaces without newline at end should end with newline' '
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$sss" &&
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$ttt$sss" &&
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$ttt$ttt$sss" &&
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$sss$sss" &&
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$ttt$sss$sss" &&
	test_stdout_line_count -gt 0 printf_git_stripspace "$ttt$sss$sss$sss"
'

test_expect_success 'text plus spaces without newline at end should not show spaces' '
	printf "$ttt$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	printf "$ttt$ttt$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	printf "$ttt$ttt$ttt$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	printf "$ttt$sss$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	printf "$ttt$ttt$sss$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	printf "$ttt$sss$sss$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null
'

test_expect_success 'text plus spaces without newline should show the correct lines' '
	printf "$ttt\n" >expect &&
	printf "$ttt$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n" >expect &&
	printf "$ttt$sss$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n" >expect &&
	printf "$ttt$sss$sss$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt\n" >expect &&
	printf "$ttt$ttt$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt\n" >expect &&
	printf "$ttt$ttt$sss$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt$ttt\n" >expect &&
	printf "$ttt$ttt$ttt$sss" | git stripspace >actual &&
	test_cmp expect actual
'

test_expect_success 'text plus spaces at end should not show spaces' '
	echo "$ttt$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	echo "$ttt$ttt$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	echo "$ttt$ttt$ttt$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	echo "$ttt$sss$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	echo "$ttt$ttt$sss$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null &&
	echo "$ttt$sss$sss$sss" | git stripspace >tmp &&
	! grep "  " tmp >/dev/null
'

test_expect_success 'text plus spaces at end should be cleaned and newline must remain' '
	echo "$ttt" >expect &&
	echo "$ttt$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	echo "$ttt" >expect &&
	echo "$ttt$sss$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	echo "$ttt" >expect &&
	echo "$ttt$sss$sss$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	echo "$ttt$ttt" >expect &&
	echo "$ttt$ttt$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	echo "$ttt$ttt" >expect &&
	echo "$ttt$ttt$sss$sss" | git stripspace >actual &&
	test_cmp expect actual &&

	echo "$ttt$ttt$ttt" >expect &&
	echo "$ttt$ttt$ttt$sss" | git stripspace >actual &&
	test_cmp expect actual
'

# spaces only:

test_expect_success 'spaces with newline at end should be replaced with empty string' '
	echo | git stripspace >actual &&
	test_must_be_empty actual &&

	echo "$sss" | git stripspace >actual &&
	test_must_be_empty actual &&

	echo "$sss$sss" | git stripspace >actual &&
	test_must_be_empty actual &&

	echo "$sss$sss$sss" | git stripspace >actual &&
	test_must_be_empty actual &&

	echo "$sss$sss$sss$sss" | git stripspace >actual &&
	test_must_be_empty actual
'

test_expect_success 'spaces without newline at end should not show spaces' '
	printf "" | git stripspace >tmp &&
	! grep " " tmp >/dev/null &&
	printf "$sss" | git stripspace >tmp &&
	! grep " " tmp >/dev/null &&
	printf "$sss$sss" | git stripspace >tmp &&
	! grep " " tmp >/dev/null &&
	printf "$sss$sss$sss" | git stripspace >tmp &&
	! grep " " tmp >/dev/null &&
	printf "$sss$sss$sss$sss" | git stripspace >tmp &&
	! grep " " tmp >/dev/null
'

test_expect_success 'spaces without newline at end should be replaced with empty string' '
	printf "" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "$sss$sss" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "$sss$sss$sss" | git stripspace >actual &&
	test_must_be_empty actual &&

	printf "$sss$sss$sss$sss" | git stripspace >actual &&
	test_must_be_empty actual
'

test_expect_success 'consecutive text lines should be unchanged' '
	printf "$ttt$ttt\n$ttt\n" >expect &&
	printf "$ttt$ttt\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n$ttt$ttt\n$ttt\n" >expect &&
	printf "$ttt\n$ttt$ttt\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n$ttt\n$ttt\n$ttt$ttt\n" >expect &&
	printf "$ttt\n$ttt\n$ttt\n$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n$ttt\n\n$ttt$ttt\n$ttt\n" >expect &&
	printf "$ttt\n$ttt\n\n$ttt$ttt\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt$ttt\n\n$ttt\n$ttt$ttt\n" >expect &&
	printf "$ttt$ttt\n\n$ttt\n$ttt$ttt\n" | git stripspace >actual &&
	test_cmp expect actual &&

	printf "$ttt\n$ttt$ttt\n\n$ttt\n" >expect &&
	printf "$ttt\n$ttt$ttt\n\n$ttt\n" | git stripspace >actual &&
	test_cmp expect actual
'

test_expect_success 'strip comments, too' '
	test ! -z "$(echo "# comment" | git stripspace)" &&
	test -z "$(echo "# comment" | git stripspace -s)"
'

test_expect_success 'strip comments with changed comment char' '
	test ! -z "$(echo "; comment" | git -c core.commentchar=";" stripspace)" &&
	test -z "$(echo "; comment" | git -c core.commentchar=";" stripspace -s)"
'

test_expect_success 'strip comments with changed comment string' '
	test ! -z "$(echo "// comment" | git -c core.commentchar=// stripspace)" &&
	test -z "$(echo "// comment" | git -c core.commentchar="//" stripspace -s)"
'

test_expect_success 'newline as commentchar is forbidden' '
	test_must_fail git -c core.commentChar="$LF" stripspace -s 2>err &&
	grep "core.commentchar cannot contain newline" err
'

test_expect_success 'empty commentchar is forbidden' '
	test_must_fail git -c core.commentchar= stripspace -s 2>err &&
	grep "core.commentchar must have at least one character" err
'

test_expect_success '-c with single line' '
	printf "# foo\n" >expect &&
	printf "foo" | git stripspace -c >actual &&
	test_cmp expect actual
'

test_expect_success '-c with single line followed by empty line' '
	printf "# foo\n#\n" >expect &&
	printf "foo\n\n" | git stripspace -c >actual &&
	test_cmp expect actual
'

test_expect_success '-c with newline only' '
	printf "#\n" >expect &&
	printf "\n" | git stripspace -c >actual &&
	test_cmp expect actual
'

test_expect_success '--comment-lines with single line' '
	printf "# foo\n" >expect &&
	printf "foo" | git stripspace -c >actual &&
	test_cmp expect actual
'

test_expect_success '-c with changed comment char' '
	printf "; foo\n" >expect &&
	printf "foo" | git -c core.commentchar=";" stripspace -c >actual &&
	test_cmp expect actual
'

test_expect_success '-c with comment char defined in .git/config' '
	test_config core.commentchar = &&
	printf "= foo\n" >expect &&
	rm -fr sub &&
	mkdir sub &&
	printf "foo" | git -C sub stripspace -c >actual &&
	test_cmp expect actual
'

test_expect_success '-c outside git repository' '
	printf "# foo\n" >expect &&
	printf "foo" | nongit git stripspace -c >actual &&
	test_cmp expect actual
'

test_expect_success 'avoid SP-HT sequence in commented line' '
	printf "#\tone\n#\n# two\n" >expect &&
	printf "\tone\n\ntwo\n" | git stripspace -c >actual &&
	test_cmp expect actual
'

test_done
