#!/bin/sh

test_description='basic work tree status reporting'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	but config --global advice.statusuoption false &&
	test_cummit A &&
	test_cummit B oneside added &&
	but checkout A^0 &&
	test_cummit C oneside created
'

test_expect_success 'A/A conflict' '
	but checkout B^0 &&
	test_must_fail but merge C
'

test_expect_success 'Report path with conflict' '
	but diff --cached --name-status >actual &&
	echo "U	oneside" >expect &&
	test_cmp expect actual
'

test_expect_success 'Report new path with conflict' '
	but diff --cached --name-status HEAD^ >actual &&
	echo "U	oneside" >expect &&
	test_cmp expect actual
'

test_expect_success 'M/D conflict does not segfault' '
	cat >expect <<EOF &&
On branch side
You have unmerged paths.
  (fix conflicts and run "but cummit")
  (use "but merge --abort" to abort the merge)

Unmerged paths:
  (use "but add/rm <file>..." as appropriate to mark resolution)
	deleted by us:   foo

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	mkdir mdconflict &&
	(
		cd mdconflict &&
		but init &&
		test_cummit initial foo "" &&
		test_cummit modify foo foo &&
		but checkout -b side HEAD^ &&
		but rm foo &&
		but cummit -m delete &&
		test_must_fail but merge main &&
		test_must_fail but cummit --dry-run >../actual &&
		test_cmp ../expect ../actual &&
		but status >../actual &&
		test_cmp ../expect ../actual
	)
'

test_expect_success 'rename & unmerged setup' '
	but rm -f -r . &&
	cat "$TEST_DIRECTORY/README" >ONE &&
	but add ONE &&
	test_tick &&
	but cummit -m "One cummit with ONE" &&

	echo Modified >TWO &&
	cat ONE >>TWO &&
	cat ONE >>THREE &&
	but add TWO THREE &&
	sha1=$(but rev-parse :ONE) &&
	but rm --cached ONE &&
	(
		echo "100644 $sha1 1	ONE" &&
		echo "100644 $sha1 2	ONE" &&
		echo "100644 $sha1 3	ONE"
	) | but update-index --index-info &&
	echo Further >>THREE
'

test_expect_success 'rename & unmerged status' '
	but status -suno >actual &&
	cat >expect <<-EOF &&
	UU ONE
	AM THREE
	A  TWO
	EOF
	test_cmp expect actual
'

test_expect_success 'but diff-index --cached shows 2 added + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	A	THREE
	A	TWO
	EOF
	but diff-index --cached --name-status HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'but diff-index --cached -M shows 2 added + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	A	THREE
	A	TWO
	EOF
	but diff-index --cached -M --name-status HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'but diff-index --cached -C shows 2 copies + 1 unmerged' '
	cat >expected <<-EOF &&
	U	ONE
	C	ONE	THREE
	C	ONE	TWO
	EOF
	but diff-index --cached -C --name-status HEAD |
	sed "s/^C[0-9]*/C/g" >actual &&
	test_cmp expected actual
'


test_expect_success 'status when conflicts with add and rm advice (deleted by them)' '
	but reset --hard &&
	but checkout main &&
	test_cummit init main.txt init &&
	but checkout -b second_branch &&
	but rm main.txt &&
	but cummit -m "main.txt deleted on second_branch" &&
	test_cummit second conflict.txt second &&
	but checkout main &&
	test_cummit on_second main.txt on_second &&
	test_cummit main conflict.txt main &&
	test_must_fail but merge second_branch &&
	cat >expected <<\EOF &&
On branch main
You have unmerged paths.
  (fix conflicts and run "but cummit")
  (use "but merge --abort" to abort the merge)

Unmerged paths:
  (use "but add/rm <file>..." as appropriate to mark resolution)
	both added:      conflict.txt
	deleted by them: main.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'prepare for conflicts' '
	but reset --hard &&
	but checkout -b conflict &&
	test_cummit one main.txt one &&
	but branch conflict_second &&
	but mv main.txt sub_main.txt &&
	but cummit -m "main.txt renamed in sub_main.txt" &&
	but checkout conflict_second &&
	but mv main.txt sub_second.txt &&
	but cummit -m "main.txt renamed in sub_second.txt"
'


test_expect_success 'status when conflicts with add and rm advice (both deleted)' '
	test_must_fail but merge conflict &&
	cat >expected <<\EOF &&
On branch conflict_second
You have unmerged paths.
  (fix conflicts and run "but cummit")
  (use "but merge --abort" to abort the merge)

Unmerged paths:
  (use "but add/rm <file>..." as appropriate to mark resolution)
	both deleted:    main.txt
	added by them:   sub_main.txt
	added by us:     sub_second.txt

no changes added to cummit (use "but add" and/or "but cummit -a")
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual
'


test_expect_success 'status when conflicts with only rm advice (both deleted)' '
	but reset --hard conflict_second &&
	test_must_fail but merge conflict &&
	but add sub_main.txt &&
	but add sub_second.txt &&
	cat >expected <<\EOF &&
On branch conflict_second
You have unmerged paths.
  (fix conflicts and run "but cummit")
  (use "but merge --abort" to abort the merge)

Changes to be cummitted:
	new file:   sub_main.txt

Unmerged paths:
  (use "but rm <file>..." to mark resolution)
	both deleted:    main.txt

Untracked files not listed (use -u option to show untracked files)
EOF
	but status --untracked-files=no >actual &&
	test_cmp expected actual &&
	but reset --hard &&
	but checkout main
'

test_expect_success 'status --branch with detached HEAD' '
	but reset --hard &&
	but checkout main^0 &&
	but status --branch --porcelain >actual &&
	cat >expected <<-EOF &&
	## HEAD (no branch)
	?? .butconfig
	?? actual
	?? expect
	?? expected
	?? mdconflict/
	EOF
	test_cmp expected actual
'

## Duplicate the above test and verify --porcelain=v1 arg parsing.
test_expect_success 'status --porcelain=v1 --branch with detached HEAD' '
	but reset --hard &&
	but checkout main^0 &&
	but status --branch --porcelain=v1 >actual &&
	cat >expected <<-EOF &&
	## HEAD (no branch)
	?? .butconfig
	?? actual
	?? expect
	?? expected
	?? mdconflict/
	EOF
	test_cmp expected actual
'

## Verify parser error on invalid --porcelain argument.
test_expect_success 'status --porcelain=bogus' '
	test_must_fail but status --porcelain=bogus
'

test_done
