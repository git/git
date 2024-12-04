#!/bin/sh

test_description='test labels in pathspecs'

. ./test-lib.sh

test_expect_success 'setup a tree' '
	cat <<-\EOF >expect &&
	fileA
	fileAB
	fileAC
	fileB
	fileBC
	fileC
	fileNoLabel
	fileSetLabel
	fileUnsetLabel
	fileValue
	fileWrongLabel
	sub/fileA
	sub/fileAB
	sub/fileAC
	sub/fileB
	sub/fileBC
	sub/fileC
	sub/fileNoLabel
	sub/fileSetLabel
	sub/fileUnsetLabel
	sub/fileValue
	sub/fileWrongLabel
	EOF
	mkdir sub &&
	while read path
	do
		echo content >$path &&
		git add $path || return 1
	done <expect &&
	git commit -m "initial commit" &&
	git ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'pathspec with no attr' '
	test_must_fail git ls-files ":(attr:)"
'

test_expect_success 'pathspec with labels and non existent .gitattributes' '
	git ls-files ":(attr:label)" >actual &&
	test_must_be_empty actual
'

test_expect_success 'pathspec with labels and non existent .gitattributes (2)' '
	test_must_fail git grep content HEAD -- ":(attr:label)"
'

test_expect_success 'setup .gitattributes' '
	cat <<-\EOF >.gitattributes &&
	fileA labelA
	fileB labelB
	fileC labelC
	fileAB labelA labelB
	fileAC labelA labelC
	fileBC labelB labelC
	fileUnsetLabel -label
	fileSetLabel label
	fileValue label=foo
	fileWrongLabel label☺
	newFileA* labelA
	newFileB* labelB
	EOF
	echo fileSetLabel label1 >sub/.gitattributes &&
	git add .gitattributes sub/.gitattributes &&
	git commit -m "add attributes"
'

test_expect_success 'setup .gitignore' '
	cat <<-\EOF >.gitignore &&
	actual
	expect
	pathspec_file
	EOF
	git add .gitignore &&
	git commit -m "add gitignore"
'

test_expect_success 'check specific set attr' '
	cat <<-\EOF >expect &&
	fileSetLabel
	sub/fileSetLabel
	EOF
	git ls-files ":(attr:label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check set attr with pathspec pattern' '
	echo sub/fileSetLabel >expect &&

	git ls-files ":(attr:label)sub" >actual &&
	test_cmp expect actual &&

	git ls-files ":(attr:label)sub/" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific set attr in tree-ish' '
	cat <<-\EOF >expect &&
	HEAD:fileSetLabel
	HEAD:sub/fileSetLabel
	EOF
	git grep -l content HEAD ":(attr:label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific set attr with pathspec pattern in tree-ish' '
	echo HEAD:sub/fileSetLabel >expect &&

	git grep -l content HEAD ":(attr:label)sub" >actual &&
	test_cmp expect actual &&

	git grep -l content HEAD ":(attr:label)sub/" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific unset attr' '
	cat <<-\EOF >expect &&
	fileUnsetLabel
	sub/fileUnsetLabel
	EOF
	git ls-files ":(attr:-label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific unset attr (2)' '
	cat <<-\EOF >expect &&
	HEAD:fileUnsetLabel
	HEAD:sub/fileUnsetLabel
	EOF
	git grep -l content HEAD ":(attr:-label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific value attr' '
	cat <<-\EOF >expect &&
	fileValue
	sub/fileValue
	EOF
	git ls-files ":(attr:label=foo)" >actual &&
	test_cmp expect actual &&
	git ls-files ":(attr:label=bar)" >actual &&
	test_must_be_empty actual
'

test_expect_success 'check specific value attr (2)' '
	cat <<-\EOF >expect &&
	HEAD:fileValue
	HEAD:sub/fileValue
	EOF
	git grep -l content HEAD ":(attr:label=foo)" >actual &&
	test_cmp expect actual &&
	test_must_fail git grep -l content HEAD ":(attr:label=bar)"
'

test_expect_success 'check unspecified attr' '
	cat <<-\EOF >expect &&
	.gitattributes
	.gitignore
	fileA
	fileAB
	fileAC
	fileB
	fileBC
	fileC
	fileNoLabel
	fileWrongLabel
	sub/.gitattributes
	sub/fileA
	sub/fileAB
	sub/fileAC
	sub/fileB
	sub/fileBC
	sub/fileC
	sub/fileNoLabel
	sub/fileWrongLabel
	EOF
	git ls-files ":(attr:!label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check unspecified attr (2)' '
	cat <<-\EOF >expect &&
	HEAD:.gitattributes
	HEAD:.gitignore
	HEAD:fileA
	HEAD:fileAB
	HEAD:fileAC
	HEAD:fileB
	HEAD:fileBC
	HEAD:fileC
	HEAD:fileNoLabel
	HEAD:fileWrongLabel
	HEAD:sub/.gitattributes
	HEAD:sub/fileA
	HEAD:sub/fileAB
	HEAD:sub/fileAC
	HEAD:sub/fileB
	HEAD:sub/fileBC
	HEAD:sub/fileC
	HEAD:sub/fileNoLabel
	HEAD:sub/fileWrongLabel
	EOF
	git grep -l ^ HEAD ":(attr:!label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check multiple unspecified attr' '
	cat <<-\EOF >expect &&
	.gitattributes
	.gitignore
	fileC
	fileNoLabel
	fileWrongLabel
	sub/.gitattributes
	sub/fileC
	sub/fileNoLabel
	sub/fileWrongLabel
	EOF
	git ls-files ":(attr:!labelB !labelA !label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check label with more labels but excluded path' '
	cat <<-\EOF >expect &&
	fileAB
	fileB
	fileBC
	EOF
	git ls-files ":(attr:labelB)" ":(exclude)sub/" >actual &&
	test_cmp expect actual
'

test_expect_success 'check label excluding other labels' '
	cat <<-\EOF >expect &&
	fileAB
	fileB
	fileBC
	sub/fileAB
	sub/fileB
	EOF
	git ls-files ":(attr:labelB)" ":(exclude,attr:labelC)sub/" >actual &&
	test_cmp expect actual
'

test_expect_success 'fail on multiple attr specifiers in one pathspec item' '
	test_must_fail git ls-files . ":(attr:labelB,attr:labelC)" 2>actual &&
	test_grep "Only one" actual
'

test_expect_success 'fail if attr magic is used in places not implemented' '
	# The main purpose of this test is to check that we actually fail
	# when you attempt to use attr magic in commands that do not implement
	# attr magic. This test does not advocate check-ignore to stay that way.
	# When you teach the command to grok the pathspec, you need to find
	# another command to replace it for the test.
	test_must_fail git check-ignore ":(attr:labelB)" 2>actual &&
	test_grep "magic not supported" actual
'

test_expect_success 'check that attr magic works for git stash push' '
	cat <<-\EOF >expect &&
	A	sub/newFileA-foo
	EOF
	>sub/newFileA-foo &&
	>sub/newFileB-foo &&
	git stash push --include-untracked -- ":(exclude,attr:labelB)" &&
	git stash show --include-untracked --name-status >actual &&
	test_cmp expect actual
'

test_expect_success 'check that attr magic works for git add --all' '
	cat <<-\EOF >expect &&
	sub/newFileA-foo
	EOF
	>sub/newFileA-foo &&
	>sub/newFileB-foo &&
	git add --all ":(exclude,attr:labelB)" &&
	git diff --name-only --cached >actual &&
	git restore -W -S . &&
	test_cmp expect actual
'

test_expect_success 'check that attr magic works for git add -u' '
	cat <<-\EOF >expect &&
	sub/fileA
	EOF
	>sub/newFileA-foo &&
	>sub/newFileB-foo &&
	>sub/fileA &&
	>sub/fileB &&
	git add -u ":(exclude,attr:labelB)" &&
	git diff --name-only --cached  >actual &&
	git restore -S -W . && rm sub/new* &&
	test_cmp expect actual
'

test_expect_success 'check that attr magic works for git add <path>' '
	cat <<-\EOF >expect &&
	fileA
	fileB
	sub/fileA
	EOF
	>fileA &&
	>fileB &&
	>sub/fileA &&
	>sub/fileB &&
	git add ":(exclude,attr:labelB)sub/*" &&
	git diff --name-only --cached >actual &&
	git restore -S -W . &&
	test_cmp expect actual
'

test_expect_success 'check that attr magic works for git -add .' '
	cat <<-\EOF >expect &&
	sub/fileA
	EOF
	>fileA &&
	>fileB &&
	>sub/fileA &&
	>sub/fileB &&
	cd sub &&
	git add . ":(exclude,attr:labelB)" &&
	cd .. &&
	git diff --name-only --cached >actual &&
	git restore -S -W . &&
	test_cmp expect actual
'

test_expect_success 'check that attr magic works for git add --pathspec-from-file' '
	cat <<-\EOF >pathspec_file &&
	:(exclude,attr:labelB)
	EOF
	cat <<-\EOF >expect &&
	sub/newFileA-foo
	EOF
	>sub/newFileA-foo &&
	>sub/newFileB-foo &&
	git add --all --pathspec-from-file=pathspec_file &&
	git diff --name-only --cached >actual &&
	test_cmp expect actual
'

test_expect_success 'abort on giving invalid label on the command line' '
	test_must_fail git ls-files . ":(attr:☺)"
'

test_expect_success 'abort on asking for wrong magic' '
	test_must_fail git ls-files . ":(attr:-label=foo)" &&
	test_must_fail git ls-files . ":(attr:!label=foo)"
'

test_expect_success 'check attribute list' '
	cat <<-EOF >>.gitattributes &&
	* whitespace=indent,trail,space
	EOF
	git ls-files ":(attr:whitespace=indent\,trail\,space)" >actual &&
	git ls-files >expect &&
	test_cmp expect actual
'

test_expect_success 'backslash cannot be the last character' '
	test_must_fail git ls-files ":(attr:label=foo\\ labelA=bar)" 2>actual &&
	test_grep "not allowed as last character in attr value" actual
'

test_expect_success 'backslash cannot be used as a value' '
	test_must_fail git ls-files ":(attr:label=f\\\oo)" 2>actual &&
	test_grep "for value matching" actual
'

test_expect_success 'reading from .gitattributes in a subdirectory (1)' '
	git ls-files ":(attr:label1)" >actual &&
	test_write_lines "sub/fileSetLabel" >expect &&
	test_cmp expect actual
'

test_expect_success 'reading from .gitattributes in a subdirectory (2)' '
	git ls-files ":(attr:label1)sub" >actual &&
	test_write_lines "sub/fileSetLabel" >expect &&
	test_cmp expect actual
'

test_expect_success 'reading from .gitattributes in a subdirectory (3)' '
	git ls-files ":(attr:label1)sub/" >actual &&
	test_write_lines "sub/fileSetLabel" >expect &&
	test_cmp expect actual
'

test_expect_success POSIXPERM 'pathspec with builtin_objectmode attr can be used' '
	>mode_exec_file_1 &&

	git status -s ":(attr:builtin_objectmode=100644)mode_exec_*" >actual &&
	echo ?? mode_exec_file_1 >expect &&
	test_cmp expect actual &&

	git add mode_exec_file_1 &&
	chmod +x mode_exec_file_1 &&
	git status -s ":(attr:builtin_objectmode=100755)mode_exec_*" >actual &&
	echo AM mode_exec_file_1 >expect &&
	test_cmp expect actual
'

test_expect_success POSIXPERM 'builtin_objectmode attr can be excluded' '
	>mode_1_regular &&
	>mode_1_exec  &&
	chmod +x mode_1_exec &&
	git status -s ":(exclude,attr:builtin_objectmode=100644)" "mode_1_*" >actual &&
	echo ?? mode_1_exec >expect &&
	test_cmp expect actual &&

	git status -s ":(exclude,attr:builtin_objectmode=100755)" "mode_1_*" >actual &&
	echo ?? mode_1_regular >expect &&
	test_cmp expect actual
'

test_done
