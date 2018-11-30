#!/bin/sh

test_description='messages from rebase operation'

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

cat >expect <<\EOF
Already applied: 0001 A
Already applied: 0002 B
Committed: 0003 Z
EOF

test_expect_success 'rebase -m' '
	git rebase -m master >report &&
	sed -n -e "/^Already applied: /p" \
		-e "/^Committed: /p" report >actual &&
	test_cmp expect actual
'

test_expect_success 'rebase against master twice' '
	git rebase master >out &&
	test_i18ngrep "Current branch topic is up to date" out
'

test_expect_success 'rebase against master twice with --force' '
	git rebase --force-rebase master >out &&
	test_i18ngrep "Current branch topic is up to date, rebase forced" out
'

test_expect_success 'rebase against master twice from another branch' '
	git checkout topic^ &&
	git rebase master topic >out &&
	test_i18ngrep "Current branch topic is up to date" out
'

test_expect_success 'rebase fast-forward to master' '
	git checkout topic^ &&
	git rebase topic >out &&
	test_i18ngrep "Fast-forwarded HEAD to topic" out
'

test_expect_success 'rebase --stat' '
	git reset --hard start &&
        git rebase --stat master >diffstat.txt &&
        grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rebase w/config rebase.stat' '
	git reset --hard start &&
        git config rebase.stat true &&
        git rebase master >diffstat.txt &&
        grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rebase -n overrides config rebase.stat config' '
	git reset --hard start &&
        git config rebase.stat true &&
        git rebase -n master >diffstat.txt &&
        ! grep "^ fileX |  *1 +$" diffstat.txt
'

# Output to stderr:
#
#     "Does not point to a valid commit: invalid-ref"
#
# NEEDSWORK: This "grep" is fine in real non-C locales, but
# GIT_TEST_GETTEXT_POISON poisons the refname along with the enclosing
# error message.
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
	rebase finished: returning to refs/heads/reflog-topic
	rebase: reflog-to-rebase
	rebase: checkout reflog-onto
	EOF
	test_cmp expect actual &&

	git checkout -b reflog-prefix reflog-to-rebase &&
	GIT_REFLOG_ACTION=change-the-reflog git rebase reflog-onto &&
	git log -g --format=%gs -3 >actual &&
	cat >expect <<-\EOF &&
	rebase finished: returning to refs/heads/reflog-prefix
	change-the-reflog: reflog-to-rebase
	change-the-reflog: checkout reflog-onto
	EOF
	test_cmp expect actual
'

test_expect_success 'rebase -i onto unrelated history' '
	git init unrelated &&
	test_commit -C unrelated 1 &&
	git -C unrelated remote add -f origin "$PWD" &&
	git -C unrelated branch --set-upstream-to=origin/master &&
	git -C unrelated -c core.editor=true rebase -i -v --stat >actual &&
	test_i18ngrep "Changes to " actual &&
	test_i18ngrep "5 files changed" actual
'

test_done
