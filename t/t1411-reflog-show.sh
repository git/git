#!/bin/sh

test_description='Test reflog display routines'
. ./test-lib.sh

test_expect_success 'setup' '
	echo content >file &&
	git add file &&
	test_tick &&
	git commit -m one
'

cat >expect <<'EOF'
Reflog: HEAD@{0} (C O Mitter <committer@example.com>)
Reflog message: commit (initial): one
EOF
test_expect_success 'log -g shows reflog headers' '
	git log -g -1 >tmp &&
	grep ^Reflog <tmp >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
e46513e HEAD@{0}: commit (initial): one
EOF
test_expect_success 'oneline reflog format' '
	git log -g -1 --oneline >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
Reflog: HEAD@{Thu Apr 7 15:13:13 2005 -0700} (C O Mitter <committer@example.com>)
Reflog message: commit (initial): one
EOF
test_expect_success 'using @{now} syntax shows reflog date (multiline)' '
	git log -g -1 HEAD@{now} >tmp &&
	grep ^Reflog <tmp >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
e46513e HEAD@{Thu Apr 7 15:13:13 2005 -0700}: commit (initial): one
EOF
test_expect_success 'using @{now} syntax shows reflog date (oneline)' '
	git log -g -1 --oneline HEAD@{now} >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
Reflog: HEAD@{1112911993 -0700} (C O Mitter <committer@example.com>)
Reflog message: commit (initial): one
EOF
test_expect_success 'using --date= shows reflog date (multiline)' '
	git log -g -1 --date=raw >tmp &&
	grep ^Reflog <tmp >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
e46513e HEAD@{1112911993 -0700}: commit (initial): one
EOF
test_expect_success 'using --date= shows reflog date (oneline)' '
	git log -g -1 --oneline --date=raw >actual &&
	test_cmp expect actual
'

: >expect
test_expect_success 'empty reflog file' '
	git branch empty &&
	: >.git/logs/refs/heads/empty &&

	git log -g empty >actual &&
	test_cmp expect actual
'

test_done
