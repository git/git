#!/bin/sh

test_description='git log'

. ./test-lib.sh

test_expect_success setup '

	echo one >one &&
	git add one &&
	test_tick &&
	git commit -m initial &&

	echo ichi >one &&
	git add one &&
	test_tick &&
	git commit -m second &&

	git mv one ichi &&
	test_tick &&
	git commit -m third &&

	cp ichi ein &&
	git add ein &&
	test_tick &&
	git commit -m fourth &&

	mkdir a &&
	echo ni >a/two &&
	git add a/two &&
	test_tick &&
	git commit -m fifth  &&

	git rm a/two &&
	test_tick &&
	git commit -m sixth

'

printf "sixth\nfifth\nfourth\nthird\nsecond\ninitial" > expect
test_expect_success 'pretty' '

	git log --pretty="format:%s" > actual &&
	test_cmp expect actual
'

printf "sixth\nfifth\nfourth\nthird\nsecond\ninitial\n" > expect
test_expect_success 'pretty (tformat)' '

	git log --pretty="tformat:%s" > actual &&
	test_cmp expect actual
'

test_expect_success 'pretty (shortcut)' '

	git log --pretty="%s" > actual &&
	test_cmp expect actual
'

test_expect_success 'format' '

	git log --format="%s" > actual &&
	test_cmp expect actual
'

cat > expect << EOF
 This is
  the sixth
  commit.
 This is
  the fifth
  commit.
EOF

test_expect_success 'format %w(11,1,2)' '

	git log -2 --format="%w(11,1,2)This is the %s commit." > actual &&
	test_cmp expect actual
'

test_expect_success 'format %w(,1,2)' '

	git log -2 --format="%w(,1,2)This is%nthe %s%ncommit." > actual &&
	test_cmp expect actual
'

cat > expect << EOF
804a787 sixth
394ef78 fifth
5d31159 fourth
2fbe8c0 third
f7dab8e second
3a2fdcb initial
EOF
test_expect_success 'oneline' '

	git log --oneline > actual &&
	test_cmp expect actual
'

test_expect_success 'diff-filter=A' '

	git log --pretty="format:%s" --diff-filter=A HEAD > actual &&
	git log --pretty="format:%s" --diff-filter A HEAD > actual-separate &&
	printf "fifth\nfourth\nthird\ninitial" > expect &&
	test_cmp expect actual &&
	test_cmp expect actual-separate

'

test_expect_success 'diff-filter=M' '

	actual=$(git log --pretty="format:%s" --diff-filter=M HEAD) &&
	expect=$(echo second) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'diff-filter=D' '

	actual=$(git log --pretty="format:%s" --diff-filter=D HEAD) &&
	expect=$(echo sixth ; echo third) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'diff-filter=R' '

	actual=$(git log -M --pretty="format:%s" --diff-filter=R HEAD) &&
	expect=$(echo third) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'diff-filter=C' '

	actual=$(git log -C -C --pretty="format:%s" --diff-filter=C HEAD) &&
	expect=$(echo fourth) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

test_expect_success 'git log --follow' '

	actual=$(git log --follow --pretty="format:%s" ichi) &&
	expect=$(echo third ; echo second ; echo initial) &&
	test "$actual" = "$expect" || {
		echo Oops
		echo "Actual: $actual"
		false
	}

'

cat > expect << EOF
804a787 sixth
394ef78 fifth
5d31159 fourth
EOF
test_expect_success 'git log --no-walk <commits> sorts by commit time' '
	git log --no-walk --oneline 5d31159 804a787 394ef78 > actual &&
	test_cmp expect actual
'

test_expect_success 'git log --no-walk=sorted <commits> sorts by commit time' '
	git log --no-walk=sorted --oneline 5d31159 804a787 394ef78 > actual &&
	test_cmp expect actual
'

cat > expect << EOF
5d31159 fourth
804a787 sixth
394ef78 fifth
EOF
test_expect_success 'git log --no-walk=unsorted <commits> leaves list of commits as given' '
	git log --no-walk=unsorted --oneline 5d31159 804a787 394ef78 > actual &&
	test_cmp expect actual
'

test_expect_success 'git show <commits> leaves list of commits as given' '
	git show --oneline -s 5d31159 804a787 394ef78 > actual &&
	test_cmp expect actual
'

test_expect_success 'setup case sensitivity tests' '
	echo case >one &&
	test_tick &&
	git add one &&
	git commit -a -m Second
'

test_expect_success 'log --grep' '
	echo second >expect &&
	git log -1 --pretty="tformat:%s" --grep=sec >actual &&
	test_cmp expect actual
'

test_expect_success 'log --grep option parsing' '
	echo second >expect &&
	git log -1 --pretty="tformat:%s" --grep sec >actual &&
	test_cmp expect actual &&
	test_must_fail git log -1 --pretty="tformat:%s" --grep
'

test_expect_success 'log -i --grep' '
	echo Second >expect &&
	git log -1 --pretty="tformat:%s" -i --grep=sec >actual &&
	test_cmp expect actual
'

test_expect_success 'log --grep -i' '
	echo Second >expect &&
	git log -1 --pretty="tformat:%s" --grep=sec -i >actual &&
	test_cmp expect actual
'

test_expect_success 'log -F -E --grep=<ere> uses ere' '
	echo second >expect &&
	git log -1 --pretty="tformat:%s" -F -E --grep=s.c.nd >actual &&
	test_cmp expect actual
'

cat > expect <<EOF
* Second
* sixth
* fifth
* fourth
* third
* second
* initial
EOF

test_expect_success 'simple log --graph' '
	git log --graph --pretty=tformat:%s >actual &&
	test_cmp expect actual
'

test_expect_success 'set up merge history' '
	git checkout -b side HEAD~4 &&
	test_commit side-1 1 1 &&
	test_commit side-2 2 2 &&
	git checkout master &&
	git merge side
'

cat > expect <<\EOF
*   Merge branch 'side'
|\
| * side-2
| * side-1
* | Second
* | sixth
* | fifth
* | fourth
|/
* third
* second
* initial
EOF

test_expect_success 'log --graph with merge' '
	git log --graph --date-order --pretty=tformat:%s |
		sed "s/ *\$//" >actual &&
	test_cmp expect actual
'

test_expect_success 'log --raw --graph -m with merge' '
	git log --raw --graph --oneline -m master | head -n 500 >actual &&
	grep "initial" actual
'

test_expect_success 'diff-tree --graph' '
	git diff-tree --graph master^ | head -n 500 >actual &&
	grep "one" actual
'

cat > expect <<\EOF
*   commit master
|\  Merge: A B
| | Author: A U Thor <author@example.com>
| |
| |     Merge branch 'side'
| |
| * commit side
| | Author: A U Thor <author@example.com>
| |
| |     side-2
| |
| * commit tags/side-1
| | Author: A U Thor <author@example.com>
| |
| |     side-1
| |
* | commit master~1
| | Author: A U Thor <author@example.com>
| |
| |     Second
| |
* | commit master~2
| | Author: A U Thor <author@example.com>
| |
| |     sixth
| |
* | commit master~3
| | Author: A U Thor <author@example.com>
| |
| |     fifth
| |
* | commit master~4
|/  Author: A U Thor <author@example.com>
|
|       fourth
|
* commit tags/side-1~1
| Author: A U Thor <author@example.com>
|
|     third
|
* commit tags/side-1~2
| Author: A U Thor <author@example.com>
|
|     second
|
* commit tags/side-1~3
  Author: A U Thor <author@example.com>

      initial
EOF

test_expect_success 'log --graph with full output' '
	git log --graph --date-order --pretty=short |
		git name-rev --name-only --stdin |
		sed "s/Merge:.*/Merge: A B/;s/ *\$//" >actual &&
	test_cmp expect actual
'

test_expect_success 'set up more tangled history' '
	git checkout -b tangle HEAD~6 &&
	test_commit tangle-a tangle-a a &&
	git merge master~3 &&
	git merge side~1 &&
	git checkout master &&
	git merge tangle &&
	git checkout -b reach &&
	test_commit reach &&
	git checkout master &&
	git checkout -b octopus-a &&
	test_commit octopus-a &&
	git checkout master &&
	git checkout -b octopus-b &&
	test_commit octopus-b &&
	git checkout master &&
	test_commit seventh &&
	git merge octopus-a octopus-b &&
	git merge reach
'

cat > expect <<\EOF
*   Merge tag 'reach'
|\
| \
|  \
*-. \   Merge tags 'octopus-a' and 'octopus-b'
|\ \ \
* | | | seventh
| | * | octopus-b
| |/ /
|/| |
| * | octopus-a
|/ /
| * reach
|/
*   Merge branch 'tangle'
|\
| *   Merge branch 'side' (early part) into tangle
| |\
| * \   Merge branch 'master' (early part) into tangle
| |\ \
| * | | tangle-a
* | | |   Merge branch 'side'
|\ \ \ \
| * | | | side-2
| | |_|/
| |/| |
| * | | side-1
* | | | Second
* | | | sixth
| |_|/
|/| |
* | | fifth
* | | fourth
|/ /
* | third
|/
* second
* initial
EOF

test_expect_success 'log --graph with merge' '
	git log --graph --date-order --pretty=tformat:%s |
		sed "s/ *\$//" >actual &&
	test_cmp expect actual
'

test_expect_success 'log.decorate configuration' '
	test_might_fail git config --unset-all log.decorate &&

	git log --oneline >expect.none &&
	git log --oneline --decorate >expect.short &&
	git log --oneline --decorate=full >expect.full &&

	echo "[log] decorate" >>.git/config &&
	git log --oneline >actual &&
	test_cmp expect.short actual &&

	git config --unset-all log.decorate &&
	git config log.decorate true &&
	git log --oneline >actual &&
	test_cmp expect.short actual &&
	git log --oneline --decorate=full >actual &&
	test_cmp expect.full actual &&
	git log --oneline --decorate=no >actual &&
	test_cmp expect.none actual &&

	git config --unset-all log.decorate &&
	git config log.decorate no &&
	git log --oneline >actual &&
	test_cmp expect.none actual &&
	git log --oneline --decorate >actual &&
	test_cmp expect.short actual &&
	git log --oneline --decorate=full >actual &&
	test_cmp expect.full actual &&

	git config --unset-all log.decorate &&
	git config log.decorate 1 &&
	git log --oneline >actual &&
	test_cmp expect.short actual &&
	git log --oneline --decorate=full >actual &&
	test_cmp expect.full actual &&
	git log --oneline --decorate=no >actual &&
	test_cmp expect.none actual &&

	git config --unset-all log.decorate &&
	git config log.decorate short &&
	git log --oneline >actual &&
	test_cmp expect.short actual &&
	git log --oneline --no-decorate >actual &&
	test_cmp expect.none actual &&
	git log --oneline --decorate=full >actual &&
	test_cmp expect.full actual &&

	git config --unset-all log.decorate &&
	git config log.decorate full &&
	git log --oneline >actual &&
	test_cmp expect.full actual &&
	git log --oneline --no-decorate >actual &&
	test_cmp expect.none actual &&
	git log --oneline --decorate >actual &&
	test_cmp expect.short actual

	git config --unset-all log.decorate &&
	git log --pretty=raw >expect.raw &&
	git config log.decorate full &&
	git log --pretty=raw >actual &&
	test_cmp expect.raw actual

'

test_expect_success 'reflog is expected format' '
	test_might_fail git config --remove-section log &&
	git log -g --abbrev-commit --pretty=oneline >expect &&
	git reflog >actual &&
	test_cmp expect actual
'

test_expect_success 'whatchanged is expected format' '
	git log --no-merges --raw >expect &&
	git whatchanged >actual &&
	test_cmp expect actual
'

test_expect_success 'log.abbrevCommit configuration' '
	test_when_finished "git config --unset log.abbrevCommit" &&

	test_might_fail git config --unset log.abbrevCommit &&

	git log --abbrev-commit >expect.log.abbrev &&
	git log --no-abbrev-commit >expect.log.full &&
	git log --pretty=raw >expect.log.raw &&
	git reflog --abbrev-commit >expect.reflog.abbrev &&
	git reflog --no-abbrev-commit >expect.reflog.full &&
	git whatchanged --abbrev-commit >expect.whatchanged.abbrev &&
	git whatchanged --no-abbrev-commit >expect.whatchanged.full &&

	git config log.abbrevCommit true &&

	git log >actual &&
	test_cmp expect.log.abbrev actual &&
	git log --no-abbrev-commit >actual &&
	test_cmp expect.log.full actual &&

	git log --pretty=raw >actual &&
	test_cmp expect.log.raw actual &&

	git reflog >actual &&
	test_cmp expect.reflog.abbrev actual &&
	git reflog --no-abbrev-commit >actual &&
	test_cmp expect.reflog.full actual &&

	git whatchanged >actual &&
	test_cmp expect.whatchanged.abbrev actual &&
	git whatchanged --no-abbrev-commit >actual &&
	test_cmp expect.whatchanged.full actual
'

test_expect_success 'show added path under "--follow -M"' '
	# This tests for a regression introduced in v1.7.2-rc0~103^2~2
	test_create_repo regression &&
	(
		cd regression &&
		test_commit needs-another-commit &&
		test_commit foo.bar &&
		git log -M --follow -p foo.bar.t &&
		git log -M --follow --stat foo.bar.t &&
		git log -M --follow --name-only foo.bar.t
	)
'

cat >expect <<\EOF
*   commit COMMIT_OBJECT_NAME
|\  Merge: MERGE_PARENTS
| | Author: A U Thor <author@example.com>
| |
| |     Merge HEADS DESCRIPTION
| |
| * commit COMMIT_OBJECT_NAME
| | Author: A U Thor <author@example.com>
| |
| |     reach
| | ---
| |  reach.t | 1 +
| |  1 file changed, 1 insertion(+)
| |
| | diff --git a/reach.t b/reach.t
| | new file mode 100644
| | index 0000000..10c9591
| | --- /dev/null
| | +++ b/reach.t
| | @@ -0,0 +1 @@
| | +reach
| |
|  \
*-. \   commit COMMIT_OBJECT_NAME
|\ \ \  Merge: MERGE_PARENTS
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     Merge HEADS DESCRIPTION
| | | |
| | * | commit COMMIT_OBJECT_NAME
| | |/  Author: A U Thor <author@example.com>
| | |
| | |       octopus-b
| | |   ---
| | |    octopus-b.t | 1 +
| | |    1 file changed, 1 insertion(+)
| | |
| | |   diff --git a/octopus-b.t b/octopus-b.t
| | |   new file mode 100644
| | |   index 0000000..d5fcad0
| | |   --- /dev/null
| | |   +++ b/octopus-b.t
| | |   @@ -0,0 +1 @@
| | |   +octopus-b
| | |
| * | commit COMMIT_OBJECT_NAME
| |/  Author: A U Thor <author@example.com>
| |
| |       octopus-a
| |   ---
| |    octopus-a.t | 1 +
| |    1 file changed, 1 insertion(+)
| |
| |   diff --git a/octopus-a.t b/octopus-a.t
| |   new file mode 100644
| |   index 0000000..11ee015
| |   --- /dev/null
| |   +++ b/octopus-a.t
| |   @@ -0,0 +1 @@
| |   +octopus-a
| |
* | commit COMMIT_OBJECT_NAME
|/  Author: A U Thor <author@example.com>
|
|       seventh
|   ---
|    seventh.t | 1 +
|    1 file changed, 1 insertion(+)
|
|   diff --git a/seventh.t b/seventh.t
|   new file mode 100644
|   index 0000000..9744ffc
|   --- /dev/null
|   +++ b/seventh.t
|   @@ -0,0 +1 @@
|   +seventh
|
*   commit COMMIT_OBJECT_NAME
|\  Merge: MERGE_PARENTS
| | Author: A U Thor <author@example.com>
| |
| |     Merge branch 'tangle'
| |
| *   commit COMMIT_OBJECT_NAME
| |\  Merge: MERGE_PARENTS
| | | Author: A U Thor <author@example.com>
| | |
| | |     Merge branch 'side' (early part) into tangle
| | |
| * |   commit COMMIT_OBJECT_NAME
| |\ \  Merge: MERGE_PARENTS
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     Merge branch 'master' (early part) into tangle
| | | |
| * | | commit COMMIT_OBJECT_NAME
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     tangle-a
| | | | ---
| | | |  tangle-a | 1 +
| | | |  1 file changed, 1 insertion(+)
| | | |
| | | | diff --git a/tangle-a b/tangle-a
| | | | new file mode 100644
| | | | index 0000000..7898192
| | | | --- /dev/null
| | | | +++ b/tangle-a
| | | | @@ -0,0 +1 @@
| | | | +a
| | | |
* | | |   commit COMMIT_OBJECT_NAME
|\ \ \ \  Merge: MERGE_PARENTS
| | | | | Author: A U Thor <author@example.com>
| | | | |
| | | | |     Merge branch 'side'
| | | | |
| * | | | commit COMMIT_OBJECT_NAME
| | |_|/  Author: A U Thor <author@example.com>
| |/| |
| | | |       side-2
| | | |   ---
| | | |    2 | 1 +
| | | |    1 file changed, 1 insertion(+)
| | | |
| | | |   diff --git a/2 b/2
| | | |   new file mode 100644
| | | |   index 0000000..0cfbf08
| | | |   --- /dev/null
| | | |   +++ b/2
| | | |   @@ -0,0 +1 @@
| | | |   +2
| | | |
| * | | commit COMMIT_OBJECT_NAME
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     side-1
| | | | ---
| | | |  1 | 1 +
| | | |  1 file changed, 1 insertion(+)
| | | |
| | | | diff --git a/1 b/1
| | | | new file mode 100644
| | | | index 0000000..d00491f
| | | | --- /dev/null
| | | | +++ b/1
| | | | @@ -0,0 +1 @@
| | | | +1
| | | |
* | | | commit COMMIT_OBJECT_NAME
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     Second
| | | | ---
| | | |  one | 1 +
| | | |  1 file changed, 1 insertion(+)
| | | |
| | | | diff --git a/one b/one
| | | | new file mode 100644
| | | | index 0000000..9a33383
| | | | --- /dev/null
| | | | +++ b/one
| | | | @@ -0,0 +1 @@
| | | | +case
| | | |
* | | | commit COMMIT_OBJECT_NAME
| |_|/  Author: A U Thor <author@example.com>
|/| |
| | |       sixth
| | |   ---
| | |    a/two | 1 -
| | |    1 file changed, 1 deletion(-)
| | |
| | |   diff --git a/a/two b/a/two
| | |   deleted file mode 100644
| | |   index 9245af5..0000000
| | |   --- a/a/two
| | |   +++ /dev/null
| | |   @@ -1 +0,0 @@
| | |   -ni
| | |
* | | commit COMMIT_OBJECT_NAME
| | | Author: A U Thor <author@example.com>
| | |
| | |     fifth
| | | ---
| | |  a/two | 1 +
| | |  1 file changed, 1 insertion(+)
| | |
| | | diff --git a/a/two b/a/two
| | | new file mode 100644
| | | index 0000000..9245af5
| | | --- /dev/null
| | | +++ b/a/two
| | | @@ -0,0 +1 @@
| | | +ni
| | |
* | | commit COMMIT_OBJECT_NAME
|/ /  Author: A U Thor <author@example.com>
| |
| |       fourth
| |   ---
| |    ein | 1 +
| |    1 file changed, 1 insertion(+)
| |
| |   diff --git a/ein b/ein
| |   new file mode 100644
| |   index 0000000..9d7e69f
| |   --- /dev/null
| |   +++ b/ein
| |   @@ -0,0 +1 @@
| |   +ichi
| |
* | commit COMMIT_OBJECT_NAME
|/  Author: A U Thor <author@example.com>
|
|       third
|   ---
|    ichi | 1 +
|    one  | 1 -
|    2 files changed, 1 insertion(+), 1 deletion(-)
|
|   diff --git a/ichi b/ichi
|   new file mode 100644
|   index 0000000..9d7e69f
|   --- /dev/null
|   +++ b/ichi
|   @@ -0,0 +1 @@
|   +ichi
|   diff --git a/one b/one
|   deleted file mode 100644
|   index 9d7e69f..0000000
|   --- a/one
|   +++ /dev/null
|   @@ -1 +0,0 @@
|   -ichi
|
* commit COMMIT_OBJECT_NAME
| Author: A U Thor <author@example.com>
|
|     second
| ---
|  one | 2 +-
|  1 file changed, 1 insertion(+), 1 deletion(-)
|
| diff --git a/one b/one
| index 5626abf..9d7e69f 100644
| --- a/one
| +++ b/one
| @@ -1 +1 @@
| -one
| +ichi
|
* commit COMMIT_OBJECT_NAME
  Author: A U Thor <author@example.com>

      initial
  ---
   one | 1 +
   1 file changed, 1 insertion(+)

  diff --git a/one b/one
  new file mode 100644
  index 0000000..5626abf
  --- /dev/null
  +++ b/one
  @@ -0,0 +1 @@
  +one
EOF

sanitize_output () {
	sed -e 's/ *$//' \
	    -e 's/commit [0-9a-f]*$/commit COMMIT_OBJECT_NAME/' \
	    -e 's/Merge: [ 0-9a-f]*$/Merge: MERGE_PARENTS/' \
	    -e 's/Merge tag.*/Merge HEADS DESCRIPTION/' \
	    -e 's/Merge commit.*/Merge HEADS DESCRIPTION/' \
	    -e 's/, 0 deletions(-)//' \
	    -e 's/, 0 insertions(+)//' \
	    -e 's/ 1 files changed, / 1 file changed, /' \
	    -e 's/, 1 deletions(-)/, 1 deletion(-)/' \
	    -e 's/, 1 insertions(+)/, 1 insertion(+)/'
}

test_expect_success 'log --graph with diff and stats' '
	git log --graph --pretty=short --stat -p >actual &&
	sanitize_output >actual.sanitized <actual &&
	test_i18ncmp expect actual.sanitized
'

test_expect_success 'dotdot is a parent directory' '
	mkdir -p a/b &&
	( echo sixth && echo fifth ) >expect &&
	( cd a/b && git log --format=%s .. ) >actual &&
	test_cmp expect actual
'

test_done
