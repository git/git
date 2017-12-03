#!/bin/sh

test_description='Basic subproject functionality'
. ./test-lib.sh

test_expect_success 'setup: create superproject' '
	: >Makefile &&
	git add Makefile &&
	git commit -m "Superproject created"
'

test_expect_success 'setup: create subprojects' '
	mkdir sub1 &&
	( cd sub1 && git init && : >Makefile && git add * &&
	git commit -q -m "subproject 1" ) &&
	mkdir sub2 &&
	( cd sub2 && git init && : >Makefile && git add * &&
	git commit -q -m "subproject 2" ) &&
	git update-index --add sub1 &&
	git add sub2 &&
	git commit -q -m "subprojects added" &&
	GIT_PRINT_SHA1_ELLIPSIS="yes" git diff-tree --abbrev=5 HEAD^ HEAD |cut -d" " -f-3,5- >current &&
	git branch save HEAD &&
	cat >expected <<-\EOF &&
	:000000 160000 00000... A	sub1
	:000000 160000 00000... A	sub2
	EOF
	test_cmp expected current
'

test_expect_success 'check if fsck ignores the subprojects' '
	git fsck --full
'

test_expect_success 'check if commit in a subproject detected' '
	( cd sub1 &&
	echo "all:" >>Makefile &&
	echo "	true" >>Makefile &&
	git commit -q -a -m "make all" ) &&
	test_expect_code 1 git diff-files --exit-code
'

test_expect_success 'check if a changed subproject HEAD can be committed' '
	git commit -q -a -m "sub1 changed" &&
	test_expect_code 1 git diff-tree --exit-code HEAD^ HEAD
'

test_expect_success 'check if diff-index works for subproject elements' '
	test_expect_code 1 git diff-index --exit-code --cached save -- sub1
'

test_expect_success 'check if diff-tree works for subproject elements' '
	test_expect_code 1 git diff-tree --exit-code HEAD^ HEAD -- sub1
'

test_expect_success 'check if git diff works for subproject elements' '
	test_expect_code 1 git diff --exit-code HEAD^ HEAD
'

test_expect_success 'check if clone works' '
	git ls-files -s >expected &&
	git clone -l -s . cloned &&
	( cd cloned && git ls-files -s ) >current &&
	test_cmp expected current
'

test_expect_success 'removing and adding subproject' '
	git update-index --force-remove -- sub2 &&
	mv sub2 sub3 &&
	git add sub3 &&
	git commit -q -m "renaming a subproject" &&
	test_expect_code 1 git diff -M --name-status --exit-code HEAD^ HEAD
'

# the index must contain the object name the HEAD of the
# subproject sub1 was at the point "save"
test_expect_success 'checkout in superproject' '
	git checkout save &&
	git diff-index --exit-code --raw --cached save -- sub1
'

# just interesting what happened...
# git diff --name-status -M save master

test_done
