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

test_expect_success 'exclude only no longer errors out' '
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

test_done
