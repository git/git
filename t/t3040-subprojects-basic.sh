#!/bin/sh

test_description='Basic subproject functionality'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup: create superproject' '
	: >Makefile &&
	but add Makefile &&
	but cummit -m "Superproject created"
'

test_expect_success 'setup: create subprojects' '
	mkdir sub1 &&
	( cd sub1 && but init && : >Makefile && but add * &&
	but cummit -q -m "subproject 1" ) &&
	mkdir sub2 &&
	( cd sub2 && but init && : >Makefile && but add * &&
	but cummit -q -m "subproject 2" ) &&
	but update-index --add sub1 &&
	but add sub2 &&
	but cummit -q -m "subprojects added" &&
	BUT_PRINT_SHA1_ELLIPSIS="yes" but diff-tree --abbrev=5 HEAD^ HEAD |cut -d" " -f-3,5- >current &&
	but branch save HEAD &&
	cat >expected <<-\EOF &&
	:000000 160000 00000... A	sub1
	:000000 160000 00000... A	sub2
	EOF
	test_cmp expected current
'

test_expect_success 'check if fsck ignores the subprojects' '
	but fsck --full
'

test_expect_success 'check if cummit in a subproject detected' '
	( cd sub1 &&
	echo "all:" >>Makefile &&
	echo "	true" >>Makefile &&
	but cummit -q -a -m "make all" ) &&
	test_expect_code 1 but diff-files --exit-code
'

test_expect_success 'check if a changed subproject HEAD can be cummitted' '
	but cummit -q -a -m "sub1 changed" &&
	test_expect_code 1 but diff-tree --exit-code HEAD^ HEAD
'

test_expect_success 'check if diff-index works for subproject elements' '
	test_expect_code 1 but diff-index --exit-code --cached save -- sub1
'

test_expect_success 'check if diff-tree works for subproject elements' '
	test_expect_code 1 but diff-tree --exit-code HEAD^ HEAD -- sub1
'

test_expect_success 'check if but diff works for subproject elements' '
	test_expect_code 1 but diff --exit-code HEAD^ HEAD
'

test_expect_success 'check if clone works' '
	but ls-files -s >expected &&
	but clone -l -s . cloned &&
	( cd cloned && but ls-files -s ) >current &&
	test_cmp expected current
'

test_expect_success 'removing and adding subproject' '
	but update-index --force-remove -- sub2 &&
	mv sub2 sub3 &&
	but add sub3 &&
	but cummit -q -m "renaming a subproject" &&
	test_expect_code 1 but diff -M --name-status --exit-code HEAD^ HEAD
'

# the index must contain the object name the HEAD of the
# subproject sub1 was at the point "save"
test_expect_success 'checkout in superproject' '
	but checkout save &&
	but diff-index --exit-code --raw --cached save -- sub1
'

test_done
