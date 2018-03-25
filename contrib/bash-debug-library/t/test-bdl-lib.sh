#!/usr/bin/env bash
#
# Test bdl-lib.sh

BASE_GIT_DIR=$(pwd)/../../..
BDL_LIB_DIR=${BASE_GIT_DIR}/contrib/bash-debug-library
TEST_DIRECTORY=${BASE_GIT_DIR}/t
export TEST_DIRECTORY

test_description='test bash debug library'

# Only execute if shell is bash
if test "$BASH_VERSION" != ""
then

. ${BDL_LIB_DIR}/bdl-lib.sh
bdl_dst=output

fi

. ${TEST_DIRECTORY}/test-lib.sh

# Only execute if shell is bash
if test "$BASH_VERSION" != ""
then

test_expect_success 'bdl_nsl only prints nothing' '
	printf "" >output &&
	bdl_nsl &&
	printf "" >expected &&
	test_cmp expected output
'

test_expect_success 'bdl_nsl with string prints string only' '
	printf "" >output &&
	bdl_nsl "test 1" &&
	printf "test 1\n" >expected &&
	test_cmp expected output
'

test_lineno=$LINENO
test_expect_success 'bdl only print source and linenumber one leading tab' '
	printf "" >output &&
	bdl &&
	printf "test-bdl-lib.sh:$((test_lineno+3)):\n" >expected &&
	test_cmp expected output
'

test_lineno=$LINENO
test_expect_success 'bdl two on one line' '
	printf "" >output &&
	bdl && bdl &&
	printf "test-bdl-lib.sh:$((test_lineno+3)):\n" >expected &&
	printf "test-bdl-lib.sh:$((test_lineno+3)):\n" >>expected &&
	test_cmp expected output
'

test_lineno=$LINENO
test_expect_success 'bdl multiple paths only one executed and no trailing space' '
	printf "" >output &&
	if test "t" = "f"
	then
		bdl
		printf "test-bdl-lib.sh:$((test_lineno+5)):\n" >expected
		test_cmp expected output
	else
		bdl
		printf "test-bdl-lib.sh:$((test_lineno+9)):\n" >expected
		test_cmp expected output
	fi
'

test_lineno=$LINENO
test_expect_success 'bdl with slo@=xx' '
	printf "" >output &&
	bdl slo@=2 &&
	printf "test-bdl-lib.sh:$((test_lineno+3)):\n" >expected &&
	test_cmp expected output
'

test_lineno=$LINENO
test_expect_success 'bdl with slo@=xx, xx is replaced with correct value' '
	printf "" >output &&
	bdl slo@=12 &&
	printf "test-bdl-lib.sh:$((test_lineno+3)):\n" >expected &&
	test_cmp expected output
'

test_lineno=$LINENO
test_expect_success 'bdl only print source and linenumber' '
	printf "" >output &&
	bdl "hi" &&
	printf "test-bdl-lib.sh:$((test_lineno+3)): hi\n" >expected &&
	test_cmp expected output
'

test_lineno=$LINENO
test_expect_success 'bdl two on one line' '
	printf "" >output &&
	bdl "hi" && bdl "bye" &&
	printf "test-bdl-lib.sh:$((test_lineno+3)): hi\n" >expected &&
	printf "test-bdl-lib.sh:$((test_lineno+3)): bye\n" >>expected &&
	test_cmp expected output
'

test_lineno=$LINENO
test_expect_success 'bdl with string prints source and linenumber and string' '
	printf "" >output &&
	bdl "test 1" &&
	printf "test-bdl-lib.sh:$((test_lineno+3)): test 1\n" >expected &&
	test_cmp expected output
'

test_expect_success 'bdl 0 "nothing printed"' '
	printf "" >output &&
	printf "" >expected &&
	bdl 0 "nothing printed" &&
	test_cmp expected output
'

# Save current bdl_dst and restore when test completes
bdl_push
test_expect_success 'bdl bdl_dst=0 nothing printed' '
	bdl_dst=0 &&
	printf "" >output &&
	printf "" >expected &&
	bdl "nothing printed" &&
	test_cmp expected output
'
bdl_pop

# Testing subroutine calls from a test verify bdl_push/pop works
# for direct calls and nested calls
subsub_lineno=$LINENO
subsub () {
	bdl_push 0
	bdl "subsub line"
	bdl_pop
	return "0"
}

sub_lineno=$LINENO
sub () {
	bdl_push 0
	bdl "sub line"
	subsub
	bdl_pop
	return "0"
}

test_expect_success 'test calls subsub' '
	printf "" >output &&
	subsub &&
	printf "test-bdl-lib.sh:$((subsub_lineno+3)): subsub line\n" >expected &&
	test_cmp expected output
'

test_expect_success 'test sub calls subsub' '
	printf "" >output &&
	sub &&
	printf "test-bdl-lib.sh:$((sub_lineno+3)): sub line
test-bdl-lib.sh:$((subsub_lineno+3)): subsub line\n" >expected &&
	test_cmp expected output
'

fi

test_done
