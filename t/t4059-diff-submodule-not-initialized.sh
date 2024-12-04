#!/bin/sh
#
# Copyright (c) 2016 Jacob Keller, based on t4041 by Jens Lehmann
#

test_description='Test for submodule diff on non-checked out submodule

This test tries to verify that add_submodule_odb works when the submodule was
initialized previously but the checkout has since been removed.
'

. ./test-lib.sh


# Test non-UTF-8 encoding in case iconv is available.
if test_have_prereq ICONV
then
	test_encoding="ISO8859-1"
	# String "added" in German (translated with Google Translate), encoded in UTF-8,
	# used in sample commit log messages in add_file() function below.
	added=$(printf "hinzugef\303\274gt")
else
	test_encoding="UTF-8"
	added="added"
fi

add_file () {
	(
		cd "$1" &&
		shift &&
		for name
		do
			echo "$name" >"$name" &&
			git add "$name" &&
			test_tick &&
			# "git commit -m" would break MinGW, as Windows refuse to pass
			# $test_encoding encoded parameter to git.
			echo "Add $name ($added $name)" | iconv -f utf-8 -t $test_encoding |
			git -c "i18n.commitEncoding=$test_encoding" commit -F -
		done >/dev/null &&
		git rev-parse --short --verify HEAD
	)
}

commit_file () {
	test_tick &&
	git commit "$@" -m "Commit $*" >/dev/null
}

test_expect_success 'setup - submodules' '
	test_create_repo sm2 &&
	add_file . foo &&
	add_file sm2 foo1 foo2 &&
	smhead1=$(git -C sm2 rev-parse --short --verify HEAD)
'

test_expect_success 'setup - git submodule add' '
	git -c protocol.file.allow=always submodule add ./sm2 sm1 &&
	commit_file sm1 .gitmodules &&
	git diff-tree -p --no-commit-id --submodule=log HEAD -- sm1 >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$smhead1 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule directory removed' '
	rm -rf sm1 &&
	git diff-tree -p --no-commit-id --submodule=log HEAD -- sm1 >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 0000000...$smhead1 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'setup - submodule multiple commits' '
	git submodule update --checkout sm1 &&
	smhead2=$(add_file sm1 foo3 foo4) &&
	commit_file sm1 &&
	git diff-tree -p --no-commit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $smhead1..$smhead2:
	  > Add foo4 ($added foo4)
	  > Add foo3 ($added foo3)
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule removed multiple commits' '
	rm -rf sm1 &&
	git diff-tree -p --no-commit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $smhead1..$smhead2:
	  > Add foo4 ($added foo4)
	  > Add foo3 ($added foo3)
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule not initialized in new clone' '
	git clone . sm3 &&
	git -C sm3 diff-tree -p --no-commit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm1 $smhead1...$smhead2 (commits not present)
	EOF
	test_cmp expected actual
'

test_expect_success 'setup submodule moved' '
	git submodule update --checkout sm1 &&
	git mv sm1 sm4 &&
	commit_file sm4 &&
	git diff-tree -p --no-commit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm4 0000000...$smhead2 (new submodule)
	EOF
	test_cmp expected actual
'

test_expect_success 'submodule moved then removed' '
	smhead3=$(add_file sm4 foo6 foo7) &&
	commit_file sm4 &&
	rm -rf sm4 &&
	git diff-tree -p --no-commit-id --submodule=log HEAD >actual &&
	cat >expected <<-EOF &&
	Submodule sm4 $smhead2..$smhead3:
	  > Add foo7 ($added foo7)
	  > Add foo6 ($added foo6)
	EOF
	test_cmp expected actual
'

test_done
