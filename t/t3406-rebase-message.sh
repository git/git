#!/bin/sh

test_description='messages from rabassa operation'

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

test_expect_success 'rabassa -m' '
	git rabassa -m master >report &&
	sed -n -e "/^Already applied: /p" \
		-e "/^Committed: /p" report >actual &&
	test_cmp expect actual
'

test_expect_success 'rabassa against master twice' '
	git rabassa master >out &&
	test_i18ngrep "Current branch topic is up to date" out
'

test_expect_success 'rabassa against master twice with --force' '
	git rabassa --force-rabassa master >out &&
	test_i18ngrep "Current branch topic is up to date, rabassa forced" out
'

test_expect_success 'rabassa against master twice from another branch' '
	git checkout topic^ &&
	git rabassa master topic >out &&
	test_i18ngrep "Current branch topic is up to date" out
'

test_expect_success 'rabassa fast-forward to master' '
	git checkout topic^ &&
	git rabassa topic >out &&
	test_i18ngrep "Fast-forwarded HEAD to topic" out
'

test_expect_success 'rabassa --stat' '
	git reset --hard start &&
        git rabassa --stat master >diffstat.txt &&
        grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rabassa w/config rabassa.stat' '
	git reset --hard start &&
        git config rabassa.stat true &&
        git rabassa master >diffstat.txt &&
        grep "^ fileX |  *1 +$" diffstat.txt
'

test_expect_success 'rabassa -n overrides config rabassa.stat config' '
	git reset --hard start &&
        git config rabassa.stat true &&
        git rabassa -n master >diffstat.txt &&
        ! grep "^ fileX |  *1 +$" diffstat.txt
'

# Output to stderr:
#
#     "Does not point to a valid commit: invalid-ref"
#
# NEEDSWORK: This "grep" is fine in real non-C locales, but
# GETTEXT_POISON poisons the refname along with the enclosing
# error message.
test_expect_success 'rabassa --onto outputs the invalid ref' '
	test_must_fail git rabassa --onto invalid-ref HEAD HEAD 2>err &&
	test_i18ngrep "invalid-ref" err
'

test_done
