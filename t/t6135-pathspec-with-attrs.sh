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
		but add $path || return 1
	done <expect &&
	but cummit -m "initial cummit" &&
	but ls-files >actual &&
	test_cmp expect actual
'

test_expect_success 'pathspec with no attr' '
	test_must_fail but ls-files ":(attr:)"
'

test_expect_success 'pathspec with labels and non existent .butattributes' '
	but ls-files ":(attr:label)" >actual &&
	test_must_be_empty actual
'

test_expect_success 'pathspec with labels and non existent .butattributes (2)' '
	test_must_fail but grep content HEAD -- ":(attr:label)"
'

test_expect_success 'setup .butattributes' '
	cat <<-\EOF >.butattributes &&
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
	but add .butattributes &&
	but cummit -m "add attributes"
'

test_expect_success 'check specific set attr' '
	cat <<-\EOF >expect &&
	fileSetLabel
	sub/fileSetLabel
	EOF
	but ls-files ":(attr:label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific set attr (2)' '
	cat <<-\EOF >expect &&
	HEAD:fileSetLabel
	HEAD:sub/fileSetLabel
	EOF
	but grep -l content HEAD ":(attr:label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific unset attr' '
	cat <<-\EOF >expect &&
	fileUnsetLabel
	sub/fileUnsetLabel
	EOF
	but ls-files ":(attr:-label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific unset attr (2)' '
	cat <<-\EOF >expect &&
	HEAD:fileUnsetLabel
	HEAD:sub/fileUnsetLabel
	EOF
	but grep -l content HEAD ":(attr:-label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check specific value attr' '
	cat <<-\EOF >expect &&
	fileValue
	sub/fileValue
	EOF
	but ls-files ":(attr:label=foo)" >actual &&
	test_cmp expect actual &&
	but ls-files ":(attr:label=bar)" >actual &&
	test_must_be_empty actual
'

test_expect_success 'check specific value attr (2)' '
	cat <<-\EOF >expect &&
	HEAD:fileValue
	HEAD:sub/fileValue
	EOF
	but grep -l content HEAD ":(attr:label=foo)" >actual &&
	test_cmp expect actual &&
	test_must_fail but grep -l content HEAD ":(attr:label=bar)"
'

test_expect_success 'check unspecified attr' '
	cat <<-\EOF >expect &&
	.butattributes
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
	but ls-files ":(attr:!label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check unspecified attr (2)' '
	cat <<-\EOF >expect &&
	HEAD:.butattributes
	HEAD:fileA
	HEAD:fileAB
	HEAD:fileAC
	HEAD:fileB
	HEAD:fileBC
	HEAD:fileC
	HEAD:fileNoLabel
	HEAD:fileWrongLabel
	HEAD:sub/fileA
	HEAD:sub/fileAB
	HEAD:sub/fileAC
	HEAD:sub/fileB
	HEAD:sub/fileBC
	HEAD:sub/fileC
	HEAD:sub/fileNoLabel
	HEAD:sub/fileWrongLabel
	EOF
	but grep -l ^ HEAD ":(attr:!label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check multiple unspecified attr' '
	cat <<-\EOF >expect &&
	.butattributes
	fileC
	fileNoLabel
	fileWrongLabel
	sub/fileC
	sub/fileNoLabel
	sub/fileWrongLabel
	EOF
	but ls-files ":(attr:!labelB !labelA !label)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check label with more labels but excluded path' '
	cat <<-\EOF >expect &&
	fileAB
	fileB
	fileBC
	EOF
	but ls-files ":(attr:labelB)" ":(exclude)sub/" >actual &&
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
	but ls-files ":(attr:labelB)" ":(exclude,attr:labelC)sub/" >actual &&
	test_cmp expect actual
'

test_expect_success 'fail on multiple attr specifiers in one pathspec item' '
	test_must_fail but ls-files . ":(attr:labelB,attr:labelC)" 2>actual &&
	test_i18ngrep "Only one" actual
'

test_expect_success 'fail if attr magic is used places not implemented' '
	# The main purpose of this test is to check that we actually fail
	# when you attempt to use attr magic in commands that do not implement
	# attr magic. This test does not advocate but-add to stay that way,
	# though, but but-add is convenient as it has its own internal pathspec
	# parsing.
	test_must_fail but add ":(attr:labelB)" 2>actual &&
	test_i18ngrep "magic not supported" actual
'

test_expect_success 'abort on giving invalid label on the command line' '
	test_must_fail but ls-files . ":(attr:☺)"
'

test_expect_success 'abort on asking for wrong magic' '
	test_must_fail but ls-files . ":(attr:-label=foo)" &&
	test_must_fail but ls-files . ":(attr:!label=foo)"
'

test_expect_success 'check attribute list' '
	cat <<-EOF >>.butattributes &&
	* whitespace=indent,trail,space
	EOF
	but ls-files ":(attr:whitespace=indent\,trail\,space)" >actual &&
	but ls-files >expect &&
	test_cmp expect actual
'

test_expect_success 'backslash cannot be the last character' '
	test_must_fail but ls-files ":(attr:label=foo\\ labelA=bar)" 2>actual &&
	test_i18ngrep "not allowed as last character in attr value" actual
'

test_expect_success 'backslash cannot be used as a value' '
	test_must_fail but ls-files ":(attr:label=f\\\oo)" 2>actual &&
	test_i18ngrep "for value matching" actual
'

test_done
