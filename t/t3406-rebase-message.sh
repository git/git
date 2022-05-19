#!/bin/sh

test_description='messages from rebase operation'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit O fileO &&
	test_cummit X fileX &&
	test_cummit A fileA &&
	test_cummit B fileB &&
	test_cummit Y fileY &&

	but checkout -b topic O &&
	but cherry-pick A B &&
	test_cummit Z fileZ &&
	but tag start
'

test_expect_success 'rebase -m' '
	but rebase -m main >actual &&
	test_must_be_empty actual
'

test_expect_success 'rebase against main twice' '
	but rebase --apply main >out &&
	test_i18ngrep "Current branch topic is up to date" out
'

test_expect_success 'rebase against main twice with --force' '
	but rebase --force-rebase --apply main >out &&
	test_i18ngrep "Current branch topic is up to date, rebase forced" out
'

test_expect_success 'rebase against main twice from another branch' '
	but checkout topic^ &&
	but rebase --apply main topic >out &&
	test_i18ngrep "Current branch topic is up to date" out
'

test_expect_success 'rebase fast-forward to main' '
	but checkout topic^ &&
	but rebase --apply topic >out &&
	test_i18ngrep "Fast-forwarded HEAD to topic" out
'

test_expect_success 'rebase --stat' '
	but reset --hard start &&
	but rebase --stat main >diffstat.txt &&
	grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rebase w/config rebase.stat' '
	but reset --hard start &&
	but config rebase.stat true &&
	but rebase main >diffstat.txt &&
	grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rebase -n overrides config rebase.stat config' '
	but reset --hard start &&
	but config rebase.stat true &&
	but rebase -n main >diffstat.txt &&
	! grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rebase --onto outputs the invalid ref' '
	test_must_fail but rebase --onto invalid-ref HEAD HEAD 2>err &&
	test_i18ngrep "invalid-ref" err
'

test_expect_success 'error out early upon -C<n> or --whitespace=<bad>' '
	test_must_fail but rebase -Cnot-a-number HEAD 2>err &&
	test_i18ngrep "numerical value" err &&
	test_must_fail but rebase --whitespace=bad HEAD 2>err &&
	test_i18ngrep "Invalid whitespace option" err
'

test_expect_success 'GIT_REFLOG_ACTION' '
	but checkout start &&
	test_cummit reflog-onto &&
	but checkout -b reflog-topic start &&
	test_cummit reflog-to-rebase &&

	but rebase reflog-onto &&
	but log -g --format=%gs -3 >actual &&
	cat >expect <<-\EOF &&
	rebase (finish): returning to refs/heads/reflog-topic
	rebase (pick): reflog-to-rebase
	rebase (start): checkout reflog-onto
	EOF
	test_cmp expect actual &&

	but checkout -b reflog-prefix reflog-to-rebase &&
	GIT_REFLOG_ACTION=change-the-reflog but rebase reflog-onto &&
	but log -g --format=%gs -3 >actual &&
	cat >expect <<-\EOF &&
	change-the-reflog (finish): returning to refs/heads/reflog-prefix
	change-the-reflog (pick): reflog-to-rebase
	change-the-reflog (start): checkout reflog-onto
	EOF
	test_cmp expect actual
'

test_expect_success 'rebase --apply reflog' '
	but checkout -b reflog-apply start &&
	old_head_reflog="$(but log -g --format=%gs -1 HEAD)" &&

	but rebase --apply Y &&

	but log -g --format=%gs -4 HEAD >actual &&
	cat >expect <<-EOF &&
	rebase finished: returning to refs/heads/reflog-apply
	rebase: Z
	rebase: checkout Y
	$old_head_reflog
	EOF
	test_cmp expect actual &&

	but log -g --format=%gs -2 reflog-apply >actual &&
	cat >expect <<-EOF &&
	rebase finished: refs/heads/reflog-apply onto $(but rev-parse Y)
	branch: Created from start
	EOF
	test_cmp expect actual
'

test_expect_success 'rebase -i onto unrelated history' '
	but init unrelated &&
	test_cummit -C unrelated 1 &&
	but -C unrelated remote add -f origin "$PWD" &&
	but -C unrelated branch --set-upstream-to=origin/main &&
	but -C unrelated -c core.editor=true rebase -i -v --stat >actual &&
	test_i18ngrep "Changes to " actual &&
	test_i18ngrep "5 files changed" actual
'

test_done
