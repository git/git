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

test_expect_success 'reflog default format' '
	git reflog -1 >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
commit e46513e
Reflog: HEAD@{0} (C O Mitter <committer@example.com>)
Reflog message: commit (initial): one
Author: A U Thor <author@example.com>

    one
EOF
test_expect_success 'override reflog default format' '
	git reflog --format=short -1 >actual &&
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
HEAD@{Thu Apr 7 15:13:13 2005 -0700}
EOF
test_expect_success 'using @{now} syntax shows reflog date (format=%gd)' '
	git log -g -1 --format=%gd HEAD@{now} >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
Reflog: HEAD@{Thu Apr 7 15:13:13 2005 -0700} (C O Mitter <committer@example.com>)
Reflog message: commit (initial): one
EOF
test_expect_success 'using --date= shows reflog date (multiline)' '
	git log -g -1 --date=default >tmp &&
	grep ^Reflog <tmp >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
e46513e HEAD@{Thu Apr 7 15:13:13 2005 -0700}: commit (initial): one
EOF
test_expect_success 'using --date= shows reflog date (oneline)' '
	git log -g -1 --oneline --date=default >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
HEAD@{1112911993 -0700}
EOF
test_expect_success 'using --date= shows reflog date (format=%gd)' '
	git log -g -1 --format=%gd --date=raw >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
Reflog: HEAD@{0} (C O Mitter <committer@example.com>)
Reflog message: commit (initial): one
EOF
test_expect_success 'log.date does not invoke "--date" magic (multiline)' '
	test_config log.date raw &&
	git log -g -1 >tmp &&
	grep ^Reflog <tmp >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
e46513e HEAD@{0}: commit (initial): one
EOF
test_expect_success 'log.date does not invoke "--date" magic (oneline)' '
	test_config log.date raw &&
	git log -g -1 --oneline >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
HEAD@{0}
EOF
test_expect_success 'log.date does not invoke "--date" magic (format=%gd)' '
	test_config log.date raw &&
	git log -g -1 --format=%gd >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
HEAD@{0}
EOF
test_expect_success '--date magic does not override explicit @{0} syntax' '
	git log -g -1 --format=%gd --date=raw HEAD@{0} >actual &&
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
