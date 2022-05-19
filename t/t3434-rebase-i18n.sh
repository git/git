#!/bin/sh
#
# Copyright (c) 2019 Doan Tran Cong Danh
#

test_description='rebase with changing encoding

Initial setup:

1 - 2              main
 \
  3 - 4            first
   \
    5 - 6          second
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

compare_msg () {
	iconv -f "$2" -t "$3" "$TEST_DIRECTORY/t3434/$1" >expect &&
	but cat-file commit HEAD >raw &&
	sed "1,/^$/d" raw >actual &&
	test_cmp expect actual
}

test_expect_success setup '
	test_cummit one &&
	but branch first &&
	test_cummit two &&
	but switch first &&
	test_cummit three &&
	but branch second &&
	test_cummit four &&
	but switch second &&
	test_cummit five &&
	test_cummit six
'

test_expect_success 'rebase --rebase-merges update encoding eucJP to UTF-8' '
	but switch -c merge-eucJP-UTF-8 first &&
	but config i18n.cummitencoding eucJP &&
	but merge -F "$TEST_DIRECTORY/t3434/eucJP.txt" second &&
	but config i18n.cummitencoding UTF-8 &&
	but rebase --rebase-merges main &&
	compare_msg eucJP.txt eucJP UTF-8
'

test_expect_success 'rebase --rebase-merges update encoding eucJP to ISO-2022-JP' '
	but switch -c merge-eucJP-ISO-2022-JP first &&
	but config i18n.cummitencoding eucJP &&
	but merge -F "$TEST_DIRECTORY/t3434/eucJP.txt" second &&
	but config i18n.cummitencoding ISO-2022-JP &&
	but rebase --rebase-merges main &&
	compare_msg eucJP.txt eucJP ISO-2022-JP
'

test_rebase_continue_update_encode () {
	old=$1
	new=$2
	msgfile=$3
	test_expect_success "rebase --continue update from $old to $new" '
		(but rebase --abort || : abort current but-rebase failure) &&
		but switch -c conflict-$old-$new one &&
		echo for-conflict >two.t &&
		but add two.t &&
		but config i18n.cummitencoding $old &&
		but cummit -F "$TEST_DIRECTORY/t3434/$msgfile" &&
		but config i18n.cummitencoding $new &&
		test_must_fail but rebase -m main &&
		test -f .but/rebase-merge/message &&
		but stripspace <.but/rebase-merge/message >two.t &&
		but add two.t &&
		but rebase --continue &&
		compare_msg $msgfile $old $new &&
		: but-cummit assume invalid utf-8 is latin1 &&
		test_cmp expect two.t
	'
}

test_rebase_continue_update_encode ISO-8859-1 UTF-8 ISO8859-1.txt
test_rebase_continue_update_encode eucJP UTF-8 eucJP.txt
test_rebase_continue_update_encode eucJP ISO-2022-JP eucJP.txt

test_done
