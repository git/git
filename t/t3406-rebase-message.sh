#!/bin/sh

test_description='messages from rebase operation'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit O fileO &&
	test_commit X fileX &&
	test_commit A fileA &&
	test_commit B fileB &&
	test_commit Y fileY &&

	git checkout -b topic O &&
	git cherry-pick A B &&
	test_commit Z fileZ &&
	git tag start
'

test_expect_success 'rebase -m' '
	git rebase -m main >actual &&
	test_must_be_empty actual
'

test_expect_success 'rebase against main twice' '
	git rebase --apply main >out &&
	test_i18ngrep "Current branch topic is up to date" out
'

test_expect_success 'rebase against main twice with --force' '
	git rebase --force-rebase --apply main >out &&
	test_i18ngrep "Current branch topic is up to date, rebase forced" out
'

test_expect_success 'rebase against main twice from another branch' '
	git checkout topic^ &&
	git rebase --apply main topic >out &&
	test_i18ngrep "Current branch topic is up to date" out
'

test_expect_success 'rebase fast-forward to main' '
	git checkout topic^ &&
	git rebase --apply topic >out &&
	test_i18ngrep "Fast-forwarded HEAD to topic" out
'

test_expect_success 'rebase --stat' '
	git reset --hard start &&
	git rebase --stat main >diffstat.txt &&
	grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rebase w/config rebase.stat' '
	git reset --hard start &&
	git config rebase.stat true &&
	git rebase main >diffstat.txt &&
	grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rebase -n overrides config rebase.stat config' '
	git reset --hard start &&
	git config rebase.stat true &&
	git rebase -n main >diffstat.txt &&
	! grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rebase --onto outputs the invalid ref' '
	test_must_fail git rebase --onto invalid-ref HEAD HEAD 2>err &&
	test_i18ngrep "invalid-ref" err
'

test_expect_success 'error out early upon -C<n> or --whitespace=<bad>' '
	test_must_fail git rebase -Cnot-a-number HEAD 2>err &&
	test_i18ngrep "numerical value" err &&
	test_must_fail git rebase --whitespace=bad HEAD 2>err &&
	test_i18ngrep "Invalid whitespace option" err
'

test_expect_success 'GIT_REFLOG_ACTION' '
	git checkout start &&
	test_commit reflog-onto &&
	git checkout -b reflog-topic start &&
	test_commit reflog-to-rebase &&

	git rebase reflog-onto &&
	git log -g --format=%gs -3 >actual &&
	cat >expect <<-\EOF &&
	rebase (finish): returning to refs/heads/reflog-topic
	rebase (pick): reflog-to-rebase
	rebase (start): checkout reflog-onto
	EOF
	test_cmp expect actual &&

	git checkout -b reflog-prefix reflog-to-rebase &&
	GIT_REFLOG_ACTION=change-the-reflog git rebase reflog-onto &&
	git log -g --format=%gs -3 >actual &&
	cat >expect <<-\EOF &&
	change-the-reflog (finish): returning to refs/heads/reflog-prefix
	change-the-reflog (pick): reflog-to-rebase
	change-the-reflog (start): checkout reflog-onto
	EOF
	test_cmp expect actual
'

test_expect_success 'rebase -i onto unrelated history' '
	git init unrelated &&
	test_commit -C unrelated 1 &&
	git -C unrelated remote add -f origin "$PWD" &&
	git -C unrelated branch --set-upstream-to=origin/main &&
	git -C unrelated -c core.editor=true rebase -i -v --stat >actual &&
	test_i18ngrep "Changes to " actual &&
	test_i18ngrep "5 files changed" actual
'

test_done
