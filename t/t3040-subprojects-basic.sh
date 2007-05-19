#!/bin/sh

test_description='Basic subproject functionality'
. ./test-lib.sh

test_expect_success 'Super project creation' \
    ': >Makefile &&
    git add Makefile &&
    git commit -m "Superproject created"'


cat >expected <<EOF
:000000 160000 00000... A	sub1
:000000 160000 00000... A	sub2
EOF
test_expect_success 'create subprojects' \
    'mkdir sub1 &&
    ( cd sub1 && git init && : >Makefile && git add * &&
    git commit -q -m "subproject 1" ) &&
    mkdir sub2 &&
    ( cd sub2 && git init && : >Makefile && git add * &&
    git commit -q -m "subproject 2" ) &&
    git update-index --add sub1 &&
    git add sub2 &&
    git commit -q -m "subprojects added" &&
    git diff-tree --abbrev=5 HEAD^ HEAD |cut -d" " -f-3,5- >current &&
    git diff expected current'

git branch save HEAD

test_expect_success 'check if fsck ignores the subprojects' \
    'git fsck --full'

test_expect_success 'check if commit in a subproject detected' \
    '( cd sub1 &&
    echo "all:" >>Makefile &&
    echo "	true" >>Makefile &&
    git commit -q -a -m "make all" ) && {
        git diff-files --exit-code
	test $? = 1
    }'

test_expect_success 'check if a changed subproject HEAD can be committed' \
    'git commit -q -a -m "sub1 changed" && {
	git diff-tree --exit-code HEAD^ HEAD
	test $? = 1
    }'

test_expect_success 'check if diff-index works for subproject elements' \
    'git diff-index --exit-code --cached save -- sub1
    test $? = 1'

test_expect_success 'check if diff-tree works for subproject elements' \
    'git diff-tree --exit-code HEAD^ HEAD -- sub1
    test $? = 1'

test_expect_success 'check if git diff works for subproject elements' \
    'git diff --exit-code HEAD^ HEAD
    test $? = 1'

test_expect_success 'check if clone works' \
    'git ls-files -s >expected &&
    git clone -l -s . cloned &&
    ( cd cloned && git ls-files -s ) >current &&
    git diff expected current'

test_expect_success 'removing and adding subproject' \
    'git update-index --force-remove -- sub2 &&
    mv sub2 sub3 &&
    git add sub3 &&
    git commit -q -m "renaming a subproject" && {
	git diff -M --name-status --exit-code HEAD^ HEAD
	test $? = 1
    }'

# the index must contain the object name the HEAD of the
# subproject sub1 was at the point "save"
test_expect_success 'checkout in superproject' \
    'git checkout save &&
    git diff-index --exit-code --raw --cached save -- sub1'

# just interesting what happened...
# git diff --name-status -M save master

test_done
