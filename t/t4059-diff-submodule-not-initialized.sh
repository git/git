#!/bin/sh
#
# Copyright (c) 2016 Jacob Keller, based on t4041 by Jens Lehmann
#

test_description='Test for submodule diff on non-checked out submodule

This test tries to verify that add_submodule_odb works when the submodule was
initialized previously but the checkout has since been removed.
'

. ./test-lib.sh

# Tested non-UTF-8 encoding
test_encoding="ISO8859-1"

# String "added" in German (translated with Google Translate), encoded in UTF-8,
# used in sample cummit log messages in add_file() function below.
added=$(printf "hinzugef\303\274gt")

add_file () {
	(
		cd "$1" &&
		shift &&
		for name
		do
			echo "$name" >"$name" &&
			but add "$name" &&
			test_tick &&
			# "but cummit -m" would break MinGW, as Windows refuse to pass
			# $test_encoding encoded parameter to but.
			echo "Add $name ($added $name)" | iconv -f utf-8 -t $test_encoding |
			but -c "i18n.cummitEncoding=$test_encoding" cummit -F -
		done >/dev/null &&
		but rev-parse --short --verify HEAD
	)
}

cummit_file () {
	test_tick &&
	but cummit "$@" -m "cummit $*" >/dev/null
}

test_expect_success 'setup - submodules' '
	test_create_repo sm2 &&
	add_file . foo &&
	add_file sm2 foo1 foo2 &&
	smhead1=$(but -C sm2 rev-parse --short --verify HEAD)
'

test_expect_success 'setup - but submodule add' '
	but submodule add ./sm2 sm1 &&
	cummit_file sm1 .butmodules &&
	but diff-tree -p --no-cummit-id --submodule=log HEAD -- sm1 >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$smhead1 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule directory removed' '
	rm -rf sm1 &&
	but diff-tree -p --no-cummit-id --submodule=log HEAD -- sm1 >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$smhead1 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'setup - submodule multiple cummits' '
	but submodule update --checkout sm1 &&
	smhead2=$(add_file sm1 foo3 foo4) &&
	cummit_file sm1 &&
	but diff-tree -p --no-cummit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $smhead1..$smhead2:
	  > Add foo4 ($added foo4)
	  > Add foo3 ($added foo3)
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule removed multiple cummits' '
	rm -rf sm1 &&
	but diff-tree -p --no-cummit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $smhead1..$smhead2:
	  > Add foo4 ($added foo4)
	  > Add foo3 ($added foo3)
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule not initialized in new clone' '
	but clone . sm3 &&
	but -C sm3 diff-tree -p --no-cummit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $smhead1...$smhead2 (cummits not present)
	EOF
	test_cmp expected actual
'

test_expect_success 'setup submodule moved' '
	but submodule update --checkout sm1 &&
	but mv sm1 sm4 &&
	cummit_file sm4 &&
	but diff-tree -p --no-cummit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm4 0000000...$smhead2 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule moved then removed' '
	smhead3=$(add_file sm4 foo6 foo7) &&
	cummit_file sm4 &&
	rm -rf sm4 &&
	but diff-tree -p --no-cummit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm4 $smhead2..$smhead3:
	  > Add foo7 ($added foo7)
	  > Add foo6 ($added foo6)
	EOF
	test_cmp expected actual
'

test_done
