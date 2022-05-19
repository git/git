#!/bin/sh

test_description='external diff interface test'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '

	test_tick &&
	echo initial >file &&
	but add file &&
	but cummit -m initial &&

	test_tick &&
	echo second >file &&
	before=$(but hash-object file) &&
	before=$(but rev-parse --short $before) &&
	but add file &&
	but cummit -m second &&

	test_tick &&
	echo third >file
'

test_expect_success 'GIT_EXTERNAL_DIFF environment' '
	cat >expect <<-EOF &&
	file $(but rev-parse --verify HEAD:file) 100644 file $(test_oid zero) 100644
	EOF
	GIT_EXTERNAL_DIFF=echo but diff >out &&
	cut -d" " -f1,3- <out >actual &&
	test_cmp expect actual

'

test_expect_success !SANITIZE_LEAK 'GIT_EXTERNAL_DIFF environment should apply only to diff' '
	GIT_EXTERNAL_DIFF=echo but log -p -1 HEAD >out &&
	grep "^diff --but a/file b/file" out

'

test_expect_success 'GIT_EXTERNAL_DIFF environment and --no-ext-diff' '
	GIT_EXTERNAL_DIFF=echo but diff --no-ext-diff >out &&
	grep "^diff --but a/file b/file" out

'

test_expect_success SYMLINKS 'typechange diff' '
	rm -f file &&
	ln -s elif file &&

	cat >expect <<-EOF &&
	file $(but rev-parse --verify HEAD:file) 100644 $(test_oid zero) 120000
	EOF
	GIT_EXTERNAL_DIFF=echo but diff >out &&
	cut -d" " -f1,3-4,6- <out >actual &&
	test_cmp expect actual &&

	GIT_EXTERNAL_DIFF=echo but diff --no-ext-diff >actual &&
	but diff >expect &&
	test_cmp expect actual
'

test_expect_success 'diff.external' '
	but reset --hard &&
	echo third >file &&
	test_config diff.external echo &&

	cat >expect <<-EOF &&
	file $(but rev-parse --verify HEAD:file) 100644 $(test_oid zero) 100644
	EOF
	but diff >out &&
	cut -d" " -f1,3-4,6- <out >actual &&
	test_cmp expect actual
'

test_expect_success !SANITIZE_LEAK 'diff.external should apply only to diff' '
	test_config diff.external echo &&
	but log -p -1 HEAD >out &&
	grep "^diff --but a/file b/file" out
'

test_expect_success 'diff.external and --no-ext-diff' '
	test_config diff.external echo &&
	but diff --no-ext-diff >out &&
	grep "^diff --but a/file b/file" out
'

test_expect_success 'diff attribute' '
	but reset --hard &&
	echo third >file &&

	but config diff.parrot.command echo &&

	echo >.butattributes "file diff=parrot" &&

	cat >expect <<-EOF &&
	file $(but rev-parse --verify HEAD:file) 100644 $(test_oid zero) 100644
	EOF
	but diff >out &&
	cut -d" " -f1,3-4,6- <out >actual &&
	test_cmp expect actual
'

test_expect_success !SANITIZE_LEAK 'diff attribute should apply only to diff' '
	but log -p -1 HEAD >out &&
	grep "^diff --but a/file b/file" out

'

test_expect_success 'diff attribute and --no-ext-diff' '
	but diff --no-ext-diff >out &&
	grep "^diff --but a/file b/file" out

'

test_expect_success 'diff attribute' '

	but config --unset diff.parrot.command &&
	but config diff.color.command echo &&

	echo >.butattributes "file diff=color" &&

	cat >expect <<-EOF &&
	file $(but rev-parse --verify HEAD:file) 100644 $(test_oid zero) 100644
	EOF
	but diff >out &&
	cut -d" " -f1,3-4,6- <out >actual &&
	test_cmp expect actual
'

test_expect_success !SANITIZE_LEAK 'diff attribute should apply only to diff' '
	but log -p -1 HEAD >out &&
	grep "^diff --but a/file b/file" out

'

test_expect_success 'diff attribute and --no-ext-diff' '
	but diff --no-ext-diff >out &&
	grep "^diff --but a/file b/file" out

'

test_expect_success 'GIT_EXTERNAL_DIFF trumps diff.external' '
	>.butattributes &&
	test_config diff.external "echo ext-global" &&

	cat >expect <<-EOF &&
	ext-env file $(but rev-parse --verify HEAD:file) 100644 file $(test_oid zero) 100644
	EOF
	GIT_EXTERNAL_DIFF="echo ext-env" but diff >out &&
	cut -d" " -f1-2,4- <out >actual &&
	test_cmp expect actual
'

test_expect_success 'attributes trump GIT_EXTERNAL_DIFF and diff.external' '
	test_config diff.foo.command "echo ext-attribute" &&
	test_config diff.external "echo ext-global" &&
	echo "file diff=foo" >.butattributes &&

	cat >expect <<-EOF &&
	ext-attribute file $(but rev-parse --verify HEAD:file) 100644 file $(test_oid zero) 100644
	EOF
	GIT_EXTERNAL_DIFF="echo ext-env" but diff >out &&
	cut -d" " -f1-2,4- <out >actual &&
	test_cmp expect actual
'

test_expect_success 'no diff with -diff' '
	echo >.butattributes "file -diff" &&
	but diff >out &&
	grep Binary out
'

echo NULZbetweenZwords | perl -pe 'y/Z/\000/' > file

test_expect_success 'force diff with "diff"' '
	after=$(but hash-object file) &&
	after=$(but rev-parse --short $after) &&
	echo >.butattributes "file diff" &&
	but diff >actual &&
	sed -e "s/^index .*/index $before..$after 100644/" \
		"$TEST_DIRECTORY"/t4020/diff.NUL >expected-diff &&
	test_cmp expected-diff actual
'

test_expect_success 'GIT_EXTERNAL_DIFF with more than one changed files' '
	echo anotherfile > file2 &&
	but add file2 &&
	but cummit -m "added 2nd file" &&
	echo modified >file2 &&
	GIT_EXTERNAL_DIFF=echo but diff
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
	GIT_EXTERNAL_DIFF=./external-diff.sh but diff &&
	test_cmp expect counter.txt
'

test_expect_success 'GIT_EXTERNAL_DIFF generates pretty paths' '
	touch file.ext &&
	but add file.ext &&
	echo with extension > file.ext &&

	cat >expect <<-EOF &&
	file.ext file $(but rev-parse --verify HEAD:file) 100644 file.ext $(test_oid zero) 100644
	EOF
	GIT_EXTERNAL_DIFF=echo but diff file.ext >out &&
	cut -d" " -f1,3- <out >actual &&
	but update-index --force-remove file.ext &&
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
	GIT_EXTERNAL_DIFF=./fake-diff.sh but diff &&
	test $(wc -l < crlfed.txt) = $(cat crlfed.txt | keep_only_cr | wc -c)
'

test_expect_success 'diff --cached' '
	test_config core.autocrlf true &&
	but add file &&
	but update-index --assume-unchanged file &&
	echo second >file &&
	but diff --cached >actual &&
	test_cmp expected-diff actual
'

test_expect_success 'clean up crlf leftovers' '
	but update-index --no-assume-unchanged file &&
	rm -f file* &&
	but reset --hard
'

test_expect_success 'submodule diff' '
	but init sub &&
	( cd sub && test_cummit sub1 ) &&
	but add sub &&
	test_tick &&
	but cummit -m "add submodule" &&
	( cd sub && test_cummit sub2 ) &&
	write_script gather_pre_post.sh <<-\EOF &&
	echo "$1 $4" # path, mode
	cat "$2" # old file
	cat "$5" # new file
	EOF
	GIT_EXTERNAL_DIFF=./gather_pre_post.sh but diff >actual &&
	cat >expected <<-EOF &&
	sub 160000
	Subproject cummit $(but rev-parse HEAD:sub)
	Subproject cummit $(cd sub && but rev-parse HEAD)
	EOF
	test_cmp expected actual
'

test_done
