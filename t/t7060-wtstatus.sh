#!/bin/sh

test_description='basic work tree status reporting'

. ./test-lib.sh

test_expect_success setup '
	git config --global advice.statusuoption false &&
	test_commit A &&
	test_commit B oneside added &&
	git checkout A^0 &&
	test_commit C oneside created
'

test_expect_success 'A/A conflict' '
	git checkout B^0 &&
	test_must_fail git merge C
'

test_expect_success 'Report path with conflict' '
	git diff --cached --name-status >actual &&
	echo "U	oneside" >expect &&
	test_cmp expect actual
'

test_expect_success 'Report new path with conflict' '
	git diff --cached --name-status HEAD^ >actual &&
	echo "U	oneside" >expect &&
	test_cmp expect actual
'

test_expect_success 'M/D conflict does not segfault' '
	cat >expect <<EOF &&
On branch side
You have unmerged paths.
  (fix conflicts and run "git commit")
  (use "git merge --abort" to abort the merge)

Unmerged paths:
  (use "git add/rm <file>..." as appropriate to mark resolution)
	deleted by us:   foo

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	mkdir mdconflict &&
	(
		cd mdconflict &&
		git init &&
		test_commit initial foo "" &&
		test_commit modify foo foo &&
		git checkout -b side HEAD^ &&
		git rm foo &&
		git commit -m delete &&
		test_must_fail git merge master &&
		test_must_fail git commit --dry-run >../actual &&
		test_i18ncmp ../expect ../actual &&
		git status >../actual &&
		test_i18ncmp ../expect ../actual
	)
'

test_expect_success 'rename & unmerged setup' '
	git rm -f -r . &&
	cat "$TEST_DIRECTORY/README" >ONE &&
	git add ONE &&
	test_tick &&
	git commit -m "One commit with ONE" &&

	echo Modified >TWO &&
	cat ONE >>TWO &&
	cat ONE >>THREE &&
	git add TWO THREE &&
	sha1=$(git rev-parse :ONE) &&
	git rm --cached ONE &&
	(
		echo "100644 $sha1 1	ONE" &&
		echo "100644 $sha1 2	ONE" &&
		echo "100644 $sha1 3	ONE"
	) | git update-index --index-info &&
	echo Further >>THREE
'

test_expect_success 'rename & unmerged status' '
	git status -suno >actual &&
	cat >expect <<-EOF &&
	UU ONE
	AM THREE
	A  TWO
	EOF
	test_cmp expect actual
'

test_expect_success 'git diff-index --cached shows 2 added + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	A	THREE
	A	TWO
	EOF
	git diff-index --cached --name-status HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'git diff-index --cached -M shows 2 added + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	A	THREE
	A	TWO
	EOF
	git diff-index --cached -M --name-status HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'git diff-index --cached -C shows 2 copies + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	C	ONE	THREE
	C	ONE	TWO
	EOF
	git diff-index --cached -C --name-status HEAD |
	sed "s/^C[0-9]*/C/g" >actual &&
	test_cmp expected actual
'


test_expect_success 'status when conflicts with add and rm advice (deleted by them)' '
	git reset --hard &&
	git checkout master &&
	test_commit init main.txt init &&
	git checkout -b second_branch &&
	git rm main.txt &&
	git commit -m "main.txt deleted on second_branch" &&
	test_commit second conflict.txt second &&
	git checkout master &&
	test_commit on_second main.txt on_second &&
	test_commit master conflict.txt master &&
	test_must_fail git merge second_branch &&
	cat >expected <<\EOF &&
On branch master
You have unmerged paths.
  (fix conflicts and run "git commit")
  (use "git merge --abort" to abort the merge)

Unmerged paths:
  (use "git add/rm <file>..." as appropriate to mark resolution)
	both added:      conflict.txt
	deleted by them: main.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'prepare for conflicts' '
	git reset --hard &&
	git checkout -b conflict &&
	test_commit one main.txt one &&
	git branch conflict_second &&
	git mv main.txt sub_master.txt &&
	git commit -m "main.txt renamed in sub_master.txt" &&
	git checkout conflict_second &&
	git mv main.txt sub_second.txt &&
	git commit -m "main.txt renamed in sub_second.txt"
'


test_expect_success 'status when conflicts with add and rm advice (both deleted)' '
	test_must_fail git merge conflict &&
	cat >expected <<\EOF &&
On branch conflict_second
You have unmerged paths.
  (fix conflicts and run "git commit")
  (use "git merge --abort" to abort the merge)

Unmerged paths:
  (use "git add/rm <file>..." as appropriate to mark resolution)
	both deleted:    main.txt
	added by them:   sub_master.txt
	added by us:     sub_second.txt

no changes added to commit (use "git add" and/or "git commit -a")
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual
'


test_expect_success 'status when conflicts with only rm advice (both deleted)' '
	git reset --hard conflict_second &&
	test_must_fail git merge conflict &&
	git add sub_master.txt &&
	git add sub_second.txt &&
	cat >expected <<\EOF &&
On branch conflict_second
You have unmerged paths.
  (fix conflicts and run "git commit")
  (use "git merge --abort" to abort the merge)

Changes to be committed:
	new file:   sub_master.txt

Unmerged paths:
  (use "git rm <file>..." to mark resolution)
	both deleted:    main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	git status --untracked-files=no >actual &&
	test_i18ncmp expected actual &&
	git reset --hard &&
	git checkout master
'

test_expect_success 'status --branch with detached HEAD' '
	git reset --hard &&
	git checkout master^0 &&
	git status --branch --porcelain >actual &&
	cat >expected <<-EOF &&
	## HEAD (no branch)
	?? .gitconfig
	?? actual
	?? expect
	?? expected
	?? mdconflict/
	EOF
	test_i18ncmp expected actual
'

## Duplicate the above test and verify --porcelain=v1 arg parsing.
test_expect_success 'status --porcelain=v1 --branch with detached HEAD' '
	git reset --hard &&
	git checkout master^0 &&
	git status --branch --porcelain=v1 >actual &&
	cat >expected <<-EOF &&
	## HEAD (no branch)
	?? .gitconfig
	?? actual
	?? expect
	?? expected
	?? mdconflict/
	EOF
	test_i18ncmp expected actual
'

## Verify parser error on invalid --porcelain argument.
test_expect_success 'status --porcelain=bogus' '
	test_must_fail git status --porcelain=bogus
'

test_done
