#!/bin/sh

test_description='test case exclude pathspec'

. ./test-lib.sh

test_expect_success 'setup' '
	for p in file sub/file sub/sub/file sub/file2 sub/sub/sub/file sub2/file; do
		if echo $p | grep /; then
			mkdir -p $(dirname $p)
		fi &&
		: >$p &&
		git add $p &&
		git commit -m $p
	done &&
	git log --oneline --format=%s >actual &&
	cat <<EOF >expect &&
sub2/file
sub/sub/sub/file
sub/file2
sub/sub/file
sub/file
file
EOF
	test_cmp expect actual
'

test_expect_success 'exclude only pathspec uses default implicit pathspec' '
	git log --oneline --format=%s -- . ":(exclude)sub" >expect &&
	git log --oneline --format=%s -- ":(exclude)sub" >actual &&
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude sub' '
	git log --oneline --format=%s -- . ":(exclude)sub" >actual &&
	cat <<EOF >expect &&
sub2/file
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude sub/sub/file' '
	git log --oneline --format=%s -- . ":(exclude)sub/sub/file" >actual &&
	cat <<EOF >expect &&
sub2/file
sub/sub/sub/file
sub/file2
sub/file
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude sub using mnemonic' '
	git log --oneline --format=%s -- . ":!sub" >actual &&
	cat <<EOF >expect &&
sub2/file
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude :(icase)SUB' '
	git log --oneline --format=%s -- . ":(exclude,icase)SUB" >actual &&
	cat <<EOF >expect &&
sub2/file
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude sub2 from sub' '
	(
	cd sub &&
	git log --oneline --format=%s -- :/ ":/!sub2" >actual &&
	cat <<EOF >expect &&
sub/sub/sub/file
sub/file2
sub/sub/file
sub/file
file
EOF
	test_cmp expect actual
	)
'

test_expect_success 't_e_i() exclude sub/*file' '
	git log --oneline --format=%s -- . ":(exclude)sub/*file" >actual &&
	cat <<EOF >expect &&
sub2/file
sub/file2
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude :(glob)sub/*/file' '
	git log --oneline --format=%s -- . ":(exclude,glob)sub/*/file" >actual &&
	cat <<EOF >expect &&
sub2/file
sub/sub/sub/file
sub/file2
sub/file
file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude sub' '
	git ls-files -- . ":(exclude)sub" >actual &&
	cat <<EOF >expect &&
file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude sub/sub/file' '
	git ls-files -- . ":(exclude)sub/sub/file" >actual &&
	cat <<EOF >expect &&
file
sub/file
sub/file2
sub/sub/sub/file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude sub using mnemonic' '
	git ls-files -- . ":!sub" >actual &&
	cat <<EOF >expect &&
file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude :(icase)SUB' '
	git ls-files -- . ":(exclude,icase)SUB" >actual &&
	cat <<EOF >expect &&
file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude sub2 from sub' '
	(
	cd sub &&
	git ls-files -- :/ ":/!sub2" >actual &&
	cat <<EOF >expect &&
../file
file
file2
sub/file
sub/sub/file
EOF
	test_cmp expect actual
	)
'

test_expect_success 'm_p_d() exclude sub/*file' '
	git ls-files -- . ":(exclude)sub/*file" >actual &&
	cat <<EOF >expect &&
file
sub/file2
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude :(glob)sub/*/file' '
	git ls-files -- . ":(exclude,glob)sub/*/file" >actual &&
	cat <<EOF >expect &&
file
sub/file
sub/file2
sub/sub/sub/file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'multiple exclusions' '
	git ls-files -- ":^*/file2" ":^sub2" >actual &&
	cat <<-\EOF >expect &&
	file
	sub/file
	sub/sub/file
	sub/sub/sub/file
	EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude case #8' '
	git init case8 &&
	(
		cd case8 &&
		echo file >file1 &&
		echo file >file2 &&
		git add file1 file2 &&
		git commit -m twofiles &&
		git grep -l file HEAD :^file2 >actual &&
		echo HEAD:file1 >expected &&
		test_cmp expected actual &&
		git grep -l file HEAD :^file1 >actual &&
		echo HEAD:file2 >expected &&
		test_cmp expected actual
	)
'

test_expect_success 'grep --untracked PATTERN' '
	# This test is not an actual test of exclude patterns, rather it
	# is here solely to ensure that if any tests are inserted, deleted, or
	# changed above, that we still have untracked files with the expected
	# contents for the NEXT two tests.
	cat <<-\EOF >expect-grep &&
	actual
	expect
	sub/actual
	sub/expect
	EOF
	git grep -l --untracked file -- >actual-grep &&
	test_cmp expect-grep actual-grep
'

test_expect_success 'grep --untracked PATTERN :(exclude)DIR' '
	cat <<-\EOF >expect-grep &&
	actual
	expect
	EOF
	git grep -l --untracked file -- ":(exclude)sub" >actual-grep &&
	test_cmp expect-grep actual-grep
'

test_expect_success 'grep --untracked PATTERN :(exclude)*FILE' '
	cat <<-\EOF >expect-grep &&
	actual
	sub/actual
	EOF
	git grep -l --untracked file -- ":(exclude)*expect" >actual-grep &&
	test_cmp expect-grep actual-grep
'

test_done
