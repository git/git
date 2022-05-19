#!/bin/sh

test_description='CRLF renormalization'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	but config core.autocrlf false &&
	printf "LINEONE\nLINETWO\nLINETHREE\n" >LF.txt &&
	printf "LINEONE\r\nLINETWO\r\nLINETHREE\r\n" >CRLF.txt &&
	printf "LINEONE\r\nLINETWO\nLINETHREE\n" >CRLF_mix_LF.txt &&
	but add . &&
	but cummit -m initial
'

test_expect_success 'renormalize CRLF in repo' '
	echo "*.txt text=auto" >.butattributes &&
	but add --renormalize "*.txt" &&
	cat >expect <<-\EOF &&
	i/lf w/crlf attr/text=auto CRLF.txt
	i/lf w/lf attr/text=auto LF.txt
	i/lf w/mixed attr/text=auto CRLF_mix_LF.txt
	EOF
	but ls-files --eol >tmp &&
	sed -e "s/	/ /g" -e "s/  */ /g" tmp |
	sort >actual &&
	test_cmp expect actual
'

test_expect_success 'ignore-errors not mistaken for renormalize' '
	but reset --hard &&
	echo "*.txt text=auto" >.butattributes &&
	but ls-files --eol >expect &&
	but add --ignore-errors "*.txt" &&
	but ls-files --eol >actual &&
	test_cmp expect actual
'

test_done
