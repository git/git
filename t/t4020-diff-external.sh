#!/bin/sh

test_description='external diff interface test'

. ./test-lib.sh

test_expect_success setup '

	test_tick &&
	echo initial >file &&
	git add file &&
	git commit -m initial &&

	test_tick &&
	echo second >file &&
	git add file &&
	git commit -m second &&

	test_tick &&
	echo third >file
'

test_expect_success 'GIT_EXTERNAL_DIFF environment' '

	GIT_EXTERNAL_DIFF=echo git diff | {
		read path oldfile oldhex oldmode newfile newhex newmode &&
		test "z$path" = zfile &&
		test "z$oldmode" = z100644 &&
		test "z$newhex" = "z$_z40" &&
		test "z$newmode" = z100644 &&
		oh=$(git rev-parse --verify HEAD:file) &&
		test "z$oh" = "z$oldhex"
	}

'

test_expect_success 'GIT_EXTERNAL_DIFF environment should apply only to diff' '

	GIT_EXTERNAL_DIFF=echo git log -p -1 HEAD |
	grep "^diff --git a/file b/file"

'

test_expect_success 'GIT_EXTERNAL_DIFF environment and --no-ext-diff' '

	GIT_EXTERNAL_DIFF=echo git diff --no-ext-diff |
	grep "^diff --git a/file b/file"

'

test_expect_success SYMLINKS 'typechange diff' '
	rm -f file &&
	ln -s elif file &&
	GIT_EXTERNAL_DIFF=echo git diff  | {
		read path oldfile oldhex oldmode newfile newhex newmode &&
		test "z$path" = zfile &&
		test "z$oldmode" = z100644 &&
		test "z$newhex" = "z$_z40" &&
		test "z$newmode" = z120000 &&
		oh=$(git rev-parse --verify HEAD:file) &&
		test "z$oh" = "z$oldhex"
	} &&
	GIT_EXTERNAL_DIFF=echo git diff --no-ext-diff >actual &&
	git diff >expect &&
	test_cmp expect actual
'

test_expect_success 'diff.external' '
	git reset --hard &&
	echo third >file &&
	test_config diff.external echo &&
	git diff | {
		read path oldfile oldhex oldmode newfile newhex newmode &&
		test "z$path" = zfile &&
		test "z$oldmode" = z100644 &&
		test "z$newhex" = "z$_z40" &&
		test "z$newmode" = z100644 &&
		oh=$(git rev-parse --verify HEAD:file) &&
		test "z$oh" = "z$oldhex"
	}
'

test_expect_success 'diff.external should apply only to diff' '
	test_config diff.external echo &&
	git log -p -1 HEAD |
	grep "^diff --git a/file b/file"
'

test_expect_success 'diff.external and --no-ext-diff' '
	test_config diff.external echo &&
	git diff --no-ext-diff |
	grep "^diff --git a/file b/file"
'

test_expect_success 'diff attribute' '
	git reset --hard &&
	echo third >file &&

	git config diff.parrot.command echo &&

	echo >.gitattributes "file diff=parrot" &&

	git diff | {
		read path oldfile oldhex oldmode newfile newhex newmode &&
		test "z$path" = zfile &&
		test "z$oldmode" = z100644 &&
		test "z$newhex" = "z$_z40" &&
		test "z$newmode" = z100644 &&
		oh=$(git rev-parse --verify HEAD:file) &&
		test "z$oh" = "z$oldhex"
	}

'

test_expect_success 'diff attribute should apply only to diff' '

	git log -p -1 HEAD |
	grep "^diff --git a/file b/file"

'

test_expect_success 'diff attribute and --no-ext-diff' '

	git diff --no-ext-diff |
	grep "^diff --git a/file b/file"

'

test_expect_success 'diff attribute' '

	git config --unset diff.parrot.command &&
	git config diff.color.command echo &&

	echo >.gitattributes "file diff=color" &&

	git diff | {
		read path oldfile oldhex oldmode newfile newhex newmode &&
		test "z$path" = zfile &&
		test "z$oldmode" = z100644 &&
		test "z$newhex" = "z$_z40" &&
		test "z$newmode" = z100644 &&
		oh=$(git rev-parse --verify HEAD:file) &&
		test "z$oh" = "z$oldhex"
	}

'

test_expect_success 'diff attribute should apply only to diff' '

	git log -p -1 HEAD |
	grep "^diff --git a/file b/file"

'

test_expect_success 'diff attribute and --no-ext-diff' '

	git diff --no-ext-diff |
	grep "^diff --git a/file b/file"

'

test_expect_success 'GIT_EXTERNAL_DIFF trumps diff.external' '
	>.gitattributes &&
	test_config diff.external "echo ext-global" &&
	GIT_EXTERNAL_DIFF="echo ext-env" git diff | grep ext-env
'

test_expect_success 'attributes trump GIT_EXTERNAL_DIFF and diff.external' '
	test_config diff.foo.command "echo ext-attribute" &&
	test_config diff.external "echo ext-global" &&
	echo "file diff=foo" >.gitattributes &&
	GIT_EXTERNAL_DIFF="echo ext-env" git diff | grep ext-attribute
'

test_expect_success 'no diff with -diff' '
	echo >.gitattributes "file -diff" &&
	git diff | grep Binary
'

echo NULZbetweenZwords | perl -pe 'y/Z/\000/' > file

test_expect_success 'force diff with "diff"' '
	echo >.gitattributes "file diff" &&
	git diff >actual &&
	test_cmp "$TEST_DIRECTORY"/t4020/diff.NUL actual
'

test_expect_success 'GIT_EXTERNAL_DIFF with more than one changed files' '
	echo anotherfile > file2 &&
	git add file2 &&
	git commit -m "added 2nd file" &&
	echo modified >file2 &&
	GIT_EXTERNAL_DIFF=echo git diff
'

test_expect_success 'GIT_EXTERNAL_DIFF path counter/total' '
	write_script external-diff.sh <<-\EOF &&
	echo $GIT_DIFF_PATH_COUNTER of $GIT_DIFF_PATH_TOTAL >>counter.txt
	EOF
	>counter.txt &&
	cat >expect <<-\EOF &&
	1 of 2
	2 of 2
	EOF
	GIT_EXTERNAL_DIFF=./external-diff.sh git diff &&
	test_cmp expect counter.txt
'

test_expect_success 'GIT_EXTERNAL_DIFF generates pretty paths' '
	touch file.ext &&
	git add file.ext &&
	echo with extension > file.ext &&
	GIT_EXTERNAL_DIFF=echo git diff file.ext | grep ......_file\.ext &&
	git update-index --force-remove file.ext &&
	rm file.ext
'

echo "#!$SHELL_PATH" >fake-diff.sh
cat >> fake-diff.sh <<\EOF
cat $2 >> crlfed.txt
EOF
chmod a+x fake-diff.sh

keep_only_cr () {
	tr -dc '\015'
}

test_expect_success 'external diff with autocrlf = true' '
	test_config core.autocrlf true &&
	GIT_EXTERNAL_DIFF=./fake-diff.sh git diff &&
	test $(wc -l < crlfed.txt) = $(cat crlfed.txt | keep_only_cr | wc -c)
'

test_expect_success 'diff --cached' '
	test_config core.autocrlf true &&
	git add file &&
	git update-index --assume-unchanged file &&
	echo second >file &&
	git diff --cached >actual &&
	test_cmp "$TEST_DIRECTORY"/t4020/diff.NUL actual
'

test_expect_success 'clean up crlf leftovers' '
	git update-index --no-assume-unchanged file &&
	rm -f file* &&
	git reset --hard
'

test_expect_success 'submodule diff' '
	git init sub &&
	( cd sub && test_commit sub1 ) &&
	git add sub &&
	test_tick &&
	git commit -m "add submodule" &&
	( cd sub && test_commit sub2 ) &&
	write_script gather_pre_post.sh <<-\EOF &&
	echo "$1 $4" # path, mode
	cat "$2" # old file
	cat "$5" # new file
	EOF
	GIT_EXTERNAL_DIFF=./gather_pre_post.sh git diff >actual &&
	cat >expected <<-EOF &&
	sub 160000
	Subproject commit $(git rev-parse HEAD:sub)
	Subproject commit $(cd sub && git rev-parse HEAD)
	EOF
	test_cmp expected actual
'

test_done
