#!/bin/sh
#
# Copyright (c) 2008 Ping Yin
#

test_description='Summary support for submodules

This test script tries to verify the sanity of summary subcommand of but submodule.
'

# NOTE: This test script uses 'but add' instead of 'but submodule add' to add
# submodules to the superproject. Some submodule subcommands such as init and
# deinit might not work as expected in this script. t7421 does not have this
# caveat.
#
# NEEDSWORK: This test script is old fashioned and may need a big cleanup due to
# various reasons, one of them being that there are lots of commands taking place
# outside of 'test_expect_success' block, which is no longer in good-style.

. ./test-lib.sh

add_file () {
	sm=$1
	shift
	owd=$(pwd)
	cd "$sm"
	for name; do
		echo "$name" >"$name" &&
		but add "$name" &&
		test_tick &&
		but cummit -m "Add $name"
	done >/dev/null
	but rev-parse --short HEAD
	cd "$owd"
}
cummit_file () {
	test_tick &&
	but cummit "$@" -m "cummit $*" >/dev/null
}

test_create_repo sm1 &&
add_file . foo >/dev/null

head1=$(add_file sm1 foo1 foo2)

test_expect_success 'added submodule' "
	but add sm1 &&
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 0000000...$head1 (2):
	  > Add foo2

	EOF
	test_cmp expected actual
"

test_expect_success 'added submodule (subdirectory)' "
	mkdir sub &&
	(
		cd sub &&
		but submodule summary >../actual
	) &&
	cat >expected <<-EOF &&
	* ../sm1 0000000...$head1 (2):
	  > Add foo2

	EOF
	test_cmp expected actual
"

test_expect_success 'added submodule (subdirectory only)' "
	(
		cd sub &&
		but submodule summary . >../actual
	) &&
	test_must_be_empty actual
"

test_expect_success 'added submodule (subdirectory with explicit path)' "
	(
		cd sub &&
		but submodule summary ../sm1 >../actual
	) &&
	cat >expected <<-EOF &&
	* ../sm1 0000000...$head1 (2):
	  > Add foo2

	EOF
	test_cmp expected actual
"

cummit_file sm1 &&
head2=$(add_file sm1 foo3)

test_expect_success 'modified submodule(forward)' "
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head1...$head2 (1):
	  > Add foo3

	EOF
	test_cmp expected actual
"

test_expect_success 'modified submodule(forward), --files' "
	but submodule summary --files >actual &&
	cat >expected <<-EOF &&
	* sm1 $head1...$head2 (1):
	  > Add foo3

	EOF
	test_cmp expected actual
"

test_expect_success 'no ignore=all setting has any effect' "
	but config -f .butmodules submodule.sm1.path sm1 &&
	but config -f .butmodules submodule.sm1.ignore all &&
	but config submodule.sm1.ignore all &&
	but config diff.ignoreSubmodules all &&
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head1...$head2 (1):
	  > Add foo3

	EOF
	test_cmp expected actual &&
	but config --unset diff.ignoreSubmodules &&
	but config --remove-section submodule.sm1 &&
	but config -f .butmodules --remove-section submodule.sm1
"


cummit_file sm1 &&
head3=$(
	cd sm1 &&
	but reset --hard HEAD~2 >/dev/null &&
	but rev-parse --short HEAD
)

test_expect_success 'modified submodule(backward)' "
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head2...$head3 (2):
	  < Add foo3
	  < Add foo2

	EOF
	test_cmp expected actual
"

head4=$(add_file sm1 foo4 foo5) &&
head4_full=$(GIT_DIR=sm1/.but but rev-parse --verify HEAD)
test_expect_success 'modified submodule(backward and forward)' "
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head2...$head4 (4):
	  > Add foo5
	  > Add foo4
	  < Add foo3
	  < Add foo2

	EOF
	test_cmp expected actual
"

test_expect_success '--summary-limit' "
	but submodule summary -n 3 >actual &&
	cat >expected <<-EOF &&
	* sm1 $head2...$head4 (4):
	  > Add foo5
	  > Add foo4
	  < Add foo3

	EOF
	test_cmp expected actual
"

cummit_file sm1 &&
mv sm1 sm1-bak &&
echo sm1 >sm1 &&
head5=$(but hash-object sm1 | cut -c1-7) &&
but add sm1 &&
rm -f sm1 &&
mv sm1-bak sm1

test_expect_success 'typechanged submodule(submodule->blob), --cached' "
	but submodule summary --cached >actual &&
	cat >expected <<-EOF &&
	* sm1 $head4(submodule)->$head5(blob) (3):
	  < Add foo5

	EOF
	test_cmp expected actual
"

test_expect_success 'typechanged submodule(submodule->blob), --files' "
	but submodule summary --files >actual &&
	cat >expected <<-EOF &&
	* sm1 $head5(blob)->$head4(submodule) (3):
	  > Add foo5

	EOF
	test_cmp expected actual
"

rm -rf sm1 &&
but checkout-index sm1
test_expect_success 'typechanged submodule(submodule->blob)' "
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head4(submodule)->$head5(blob):

	EOF
	test_cmp expected actual
"

rm -f sm1 &&
test_create_repo sm1 &&
head6=$(add_file sm1 foo6 foo7)
test_expect_success 'nonexistent cummit' "
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head4...$head6:
	  Warn: sm1 doesn't contain cummit $head4_full

	EOF
	test_cmp expected actual
"

cummit_file
test_expect_success 'typechanged submodule(blob->submodule)' "
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head5(blob)->$head6(submodule) (2):
	  > Add foo7

	EOF
	test_cmp expected actual
"

cummit_file sm1 &&
rm -rf sm1
test_expect_success 'deleted submodule' "
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head6...0000000:

	EOF
	test_cmp expected actual
"

test_expect_success 'create second submodule' '
	test_create_repo sm2 &&
	head7=$(add_file sm2 foo8 foo9) &&
	but add sm2
'

test_expect_success 'multiple submodules' "
	but submodule summary >actual &&
	cat >expected <<-EOF &&
	* sm1 $head6...0000000:

	* sm2 0000000...$head7 (2):
	  > Add foo9

	EOF
	test_cmp expected actual
"

test_expect_success 'path filter' "
	but submodule summary sm2 >actual &&
	cat >expected <<-EOF &&
	* sm2 0000000...$head7 (2):
	  > Add foo9

	EOF
	test_cmp expected actual
"

cummit_file sm2
test_expect_success 'given cummit' "
	but submodule summary HEAD^ >actual &&
	cat >expected <<-EOF &&
	* sm1 $head6...0000000:

	* sm2 0000000...$head7 (2):
	  > Add foo9

	EOF
	test_cmp expected actual
"

test_expect_success '--for-status' "
	but submodule summary --for-status HEAD^ >actual &&
	test_cmp - actual <<-EOF
	* sm1 $head6...0000000:

	* sm2 0000000...$head7 (2):
	  > Add foo9

	EOF
"

test_expect_success 'fail when using --files together with --cached' "
	test_must_fail but submodule summary --files --cached
"

test_expect_success 'should not fail in an empty repo' "
	but init xyzzy &&
	cd xyzzy &&
	but submodule summary >output 2>&1 &&
	test_must_be_empty output
"

test_done
