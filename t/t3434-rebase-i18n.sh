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

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

if ! test_have_prereq ICONV
then
	skip_all='skipping rebase i18n tests; iconv not available'
	test_done
fi

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
	git rebase --rebase-merges main &&
	compare_msg eucJP.txt eucJP UTF-8
'

test_expect_success 'rebase --rebase-merges update encoding eucJP to ISO-2022-JP' '
	git switch -c merge-eucJP-ISO-2022-JP first &&
	git config i18n.commitencoding eucJP &&
	git merge -F "$TEST_DIRECTORY/t3434/eucJP.txt" second &&
	git config i18n.commitencoding ISO-2022-JP &&
	git rebase --rebase-merges main &&
	compare_msg eucJP.txt eucJP ISO-2022-JP
'

test_rebase_continue_update_encode () {
	old=$1
	new=$2
	msgfile=$3
	test_expect_success "rebase --continue update from $old to $new" '
		(git rebase --abort || : abort current git-rebase failure) &&
		git switch -c conflict-$old-$new one &&
		echo for-conflict >two.t &&
		git add two.t &&
		git config i18n.commitencoding $old &&
		git commit -F "$TEST_DIRECTORY/t3434/$msgfile" &&
		git config i18n.commitencoding $new &&
		test_must_fail git rebase -m main &&
		test -f .git/rebase-merge/message &&
		git stripspace -s <.git/rebase-merge/message >two.t &&
		git add two.t &&
		git rebase --continue &&
		compare_msg $msgfile $old $new &&
		: git-commit assume invalid utf-8 is latin1 &&
		test_cmp expect two.t
	'
}

test_rebase_continue_update_encode ISO-8859-1 UTF-8 ISO8859-1.txt
test_rebase_continue_update_encode eucJP UTF-8 eucJP.txt
test_rebase_continue_update_encode eucJP ISO-2022-JP eucJP.txt

test_done
