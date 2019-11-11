#!/bin/sh
#
# Copyright (c) 2019 Doan Tran Cong Danh
#

test_description='rebase with changing encoding

Initial setup:

1 - 2              master
 \
  3 - 4            first
   \
    5 - 6          second
'

. ./test-lib.sh

compare_msg () {
	iconv -f "$2" -t "$3" "$TEST_DIRECTORY/t3434/$1" >expect &&
	git cat-file commit HEAD >raw &&
	sed "1,/^$/d" raw >actual &&
	test_cmp expect actual
}

test_expect_success setup '
	test_commit one &&
	git branch first &&
	test_commit two &&
	git switch first &&
	test_commit three &&
	git branch second &&
	test_commit four &&
	git switch second &&
	test_commit five &&
	test_commit six
'

test_expect_success 'rebase --rebase-merges update encoding eucJP to UTF-8' '
	git switch -c merge-eucJP-UTF-8 first &&
	git config i18n.commitencoding eucJP &&
	git merge -F "$TEST_DIRECTORY/t3434/eucJP.txt" second &&
	git config i18n.commitencoding UTF-8 &&
	git rebase --rebase-merges master &&
	compare_msg eucJP.txt eucJP UTF-8
'

test_expect_failure 'rebase --rebase-merges update encoding eucJP to ISO-2022-JP' '
	git switch -c merge-eucJP-ISO-2022-JP first &&
	git config i18n.commitencoding eucJP &&
	git merge -F "$TEST_DIRECTORY/t3434/eucJP.txt" second &&
	git config i18n.commitencoding ISO-2022-JP &&
	git rebase --rebase-merges master &&
	compare_msg eucJP.txt eucJP ISO-2022-JP
'

test_done
