#!/bin/sh

test_description='Test reflog display routines'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	echo content >file &&
	git add file &&
	test_tick &&
	git commit -m one
'

commit=$(git rev-parse --short HEAD)
cat >expect <<'EOF'
Reflog: HEAD@{0} (C O Mitter <committer@example.com>)
Reflog message: commit (initial): one
EOF
test_expect_success 'log -g shows reflog headers' '
	git log -g -1 >tmp &&
	grep ^Reflog <tmp >actual &&
	test_cmp expect actual
'

cat >expect <<EOF
$commit HEAD@{0}: commit (initial): one
EOF
test_expect_success 'oneline reflog format' '
	git log -g -1 --oneline >actual &&
	test_cmp expect actual
'

test_expect_success 'reflog default format' '
	git reflog -1 HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '@{0} names the current branch by its short name' '
	echo "$commit main@{0}: commit (initial): one" >expect.branch &&
	git reflog -1 @{0} >actual &&
	test_cmp expect.branch actual
'

test_expect_success 'reflog shows the current branch by default' '
	git reflog -1 >actual &&
	test_cmp expect.branch actual
'

cat >expect <<EOF
commit $commit
Reflog: HEAD@{0} (C O Mitter <committer@example.com>)
Reflog message: commit (initial): one
Author: A U Thor <author@example.com>

    one
EOF
test_expect_success 'override reflog default format' '
	git reflog --format=short -1 HEAD >actual &&
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

cat >expect <<EOF
$commit HEAD@{Thu Apr 7 15:13:13 2005 -0700}: commit (initial): one
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

cat >expect <<EOF
$commit HEAD@{Thu Apr 7 15:13:13 2005 -0700}: commit (initial): one
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

cat >expect <<EOF
$commit HEAD@{0}: commit (initial): one
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

test_expect_success 'empty reflog file' '
	git branch empty &&
	git reflog expire --expire=all refs/heads/empty &&

	git log -g empty >actual &&
	test_must_be_empty actual
'

# This guards against the alternative of showing the diffs vs. the
# reflog ancestor.  The reflog used is designed to list the commits
# more than once, so as to exercise the corresponding logic.
test_expect_success 'git log -g -p shows diffs vs. parents' '
	test_commit two &&
	git branch flipflop &&
	git update-ref refs/heads/flipflop -m flip1 HEAD^ &&
	git update-ref refs/heads/flipflop -m flop1 HEAD &&
	git update-ref refs/heads/flipflop -m flip2 HEAD^ &&
	git log -g -p flipflop >reflog &&
	grep -v ^Reflog reflog >actual &&
	git log -1 -p HEAD^ >log.one &&
	git log -1 -p HEAD >log.two &&
	(
		cat log.one && echo &&
		cat log.two && echo &&
		cat log.one && echo &&
		cat log.two
	) >expect &&
	test_cmp expect actual
'

test_expect_success 'reflog shows branches in messages by their short names' '
	git branch short-names &&
	oid=$(git rev-parse HEAD) &&
	git reflog write refs/heads/short-names $oid $oid \
		"Branch: renamed refs/heads/a to refs/heads/b" &&
	echo "Branch: renamed a to b" >expect &&
	git log -g -1 --format=%gs short-names >actual &&
	test_cmp expect actual
'

test_expect_success 'reflog shows HEAD by default when it is detached' '
	test_when_finished "git checkout main" &&
	git checkout --detach &&
	git reflog HEAD >expect &&
	git reflog >actual &&
	test_cmp expect actual
'

test_expect_success 'reflog on an unborn branch' '
	test_when_finished "rm -rf unborn" &&
	git init unborn &&
	(
		cd unborn &&
		test_must_fail git reflog 2>err &&
		test_grep "does not have any commits yet" err
	)
'

test_done
