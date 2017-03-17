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
		: >$path &&
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
	EOF
	git add .gitattributes &&
	git commit -m "add attributes"
'

test_expect_success 'check specific set attr' '
	cat <<-\EOF >expect &&
	fileSetLabel
	sub/fileSetLabel
	EOF
	git ls-files ":(attr:label)" >actual &&
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

test_expect_success 'check unspecified attr' '
	cat <<-\EOF >expect &&
	.gitattributes
	fileA
	fileAB
	fileAC
	fileB
	fileBC
	fileC
	fileNoLabel
	fileWrongLabel
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

test_expect_success 'check multiple unspecified attr' '
	cat <<-\EOF >expect &&
	.gitattributes
	fileC
	fileNoLabel
	fileWrongLabel
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
	test_i18ngrep "Only one" actual
'

test_expect_success 'fail if attr magic is used places not implemented' '
	# The main purpose of this test is to check that we actually fail
	# when you attempt to use attr magic in commands that do not implement
	# attr magic. This test does not advocate git-add to stay that way,
	# though, but git-add is convenient as it has its own internal pathspec
	# parsing.
	test_must_fail git add ":(attr:labelB)" 2>actual &&
	test_i18ngrep "unsupported magic" actual
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
	test_i18ngrep "not allowed as last character in attr value" actual
'

test_expect_success 'backslash cannot be used as a value' '
	test_must_fail git ls-files ":(attr:label=f\\\oo)" 2>actual &&
	test_i18ngrep "for value matching" actual
'

test_done
