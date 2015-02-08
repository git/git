#!/bin/sh

test_description='git list-files test'

. ./test-lib.sh

test_expect_success 'setup' '
	mkdir dir &&
	touch file dir/file &&
	git init gitlink &&
	( cd gitlink && test_commit foo ) &&
	git add file dir/file gitlink &&
	git commit -qm1
'

test_expect_success 'LS_COLORS env variable' '
	LS_COLORS="rs=0:fi=31:di=32" \
		git list-files --color=always | test_decode_color | \
		grep -v gitlink >actual &&
	cat >expected <<-\EOF &&
	<GREEN>dir<RESET>
	<RED>file<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'color.ls.*' '
	test_config color.ls.file red &&
	test_config color.ls.directory green &&
	test_config color.ls.submodule yellow &&
	git list-files --color=always | test_decode_color >actual &&
	cat >expected <<-\EOF &&
	<GREEN>dir<RESET>
	<RED>file<RESET>
	<YELLOW>gitlink<RESET>
	EOF
	test_cmp expected actual
'

test_expect_success 'column output' '
	COLUMNS=20 git list-files --column=always >actual &&
	cat >expected <<-\EOF &&
	dir      gitlink
	file
	EOF
	test_cmp expected actual &&
	git list-files -1 >actual &&
	cat >expected <<-\EOF &&
	dir
	file
	gitlink
	EOF
	test_cmp expected actual
'

test_expect_success '--max-depth' '
	git list-files --max-depth=1 >actual &&
	cat >expected <<-\EOF &&
	dir/file
	file
	gitlink
	EOF
	test_cmp expected actual
'

test_expect_success 'recursive' '
	git list-files -R >actual &&
	cat >expected <<-\EOF &&
	dir/file
	file
	gitlink
	EOF
	test_cmp expected actual
'

test_expect_success 'globbing' '
	git list-files "f*" >actual &&
	cat >expected <<-\EOF &&
	file
	EOF
	test_cmp expected actual &&
	git list-files "**/f*" >actual &&
	cat >expected <<-\EOF &&
	dir/file
	file
	EOF
	test_cmp expected actual
'

test_expect_success 'no dups' '
	test_when_finished "git checkout file" &&
	echo dirty >>file &&
	git list-files -m file >actual &&
	echo "file" >expected &&
	test_cmp expected actual &&
	git list-files -cm file >actual &&
	echo "C file" >expected &&
	test_cmp expected actual &&
	git list-files -tcm file >actual &&
	test_cmp expected actual
'

test_expect_success '--classify' '
	git list-files -F >actual &&
	cat >expected <<-\EOF &&
	dir/
	file
	gitlink&
	EOF
	test_cmp expected actual
'

test_expect_success 'diff-cached' '
	test_when_finished "git checkout file" &&
	echo dirty >>file &&
	git add file &&
	git list-files -M >actual &&
	echo "file" >expected &&
	test_cmp expected actual
'

test_expect_success 'unmerged files' '
	git ls-files --stage file >index-info &&
	sed "s/ 0/ 2/;s/file/unmerged/" index-info | git update-index --index-info &&
	sed "s/ 0/ 3/;s,file,dir/unmerged," index-info | git update-index --index-info &&
	git list-files -u >actual &&
	cat >expected <<-\EOF &&
	dir
	unmerged
	EOF
	test_cmp expected actual
'

test_done
