#!/bin/sh
#
# Copyright (c) 2010, Will Palmer
# Copyright (c) 2011, Alexey Shumkin (+ non-UTF-8 commit encoding tests)
#

test_description='Test pretty formats'
. ./test-lib.sh

sample_utf8_part=$(printf "f\303\244ng")

commit_msg () {
	# String "initial. initial" partly in German
	# (translated with Google Translate),
	# encoded in UTF-8, used as a commit log message below.
	msg="initial. an${sample_utf8_part}lich\n"
	if test -n "$1"
	then
		printf "$msg" | iconv -f utf-8 -t "$1"
	else
		printf "$msg"
	fi
}

test_expect_success 'set up basic repos' '
	>foo &&
	>bar &&
	git add foo &&
	test_tick &&
	git config i18n.commitEncoding iso8859-1 &&
	git commit -m "$(commit_msg iso8859-1)" &&
	git add bar &&
	test_tick &&
	git commit -m "add bar" &&
	git config --unset i18n.commitEncoding
'

test_expect_success 'alias builtin format' '
	git log --pretty=oneline >expected &&
	git config pretty.test-alias oneline &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias masking builtin format' '
	git log --pretty=oneline >expected &&
	git config pretty.oneline "%H" &&
	git log --pretty=oneline >actual &&
	test_cmp expected actual
'

test_expect_success 'alias user-defined format' '
	git log --pretty="format:%h" >expected &&
	git config pretty.test-alias "format:%h" &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias user-defined tformat with %s (iso8859-1 encoding)' '
	git config i18n.logOutputEncoding iso8859-1 &&
	git log --oneline >expected-s &&
	git log --pretty="tformat:%h %s" >actual-s &&
	git config --unset i18n.logOutputEncoding &&
	test_cmp expected-s actual-s
'

test_expect_success 'alias user-defined tformat with %s (utf-8 encoding)' '
	git log --oneline >expected-s &&
	git log --pretty="tformat:%h %s" >actual-s &&
	test_cmp expected-s actual-s
'

test_expect_success 'alias user-defined tformat' '
	git log --pretty="tformat:%h" >expected &&
	git config pretty.test-alias "tformat:%h" &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias non-existent format' '
	git config pretty.test-alias format-that-will-never-exist &&
	test_must_fail git log --pretty=test-alias
'

test_expect_success 'alias of an alias' '
	git log --pretty="tformat:%h" >expected &&
	git config pretty.test-foo "tformat:%h" &&
	git config pretty.test-bar test-foo &&
	git log --pretty=test-bar >actual && test_cmp expected actual
'

test_expect_success 'alias masking an alias' '
	git log --pretty=format:"Two %H" >expected &&
	git config pretty.duplicate "format:One %H" &&
	git config --add pretty.duplicate "format:Two %H" &&
	git log --pretty=duplicate >actual &&
	test_cmp expected actual
'

test_expect_success 'alias loop' '
	git config pretty.test-foo test-bar &&
	git config pretty.test-bar test-foo &&
	test_must_fail git log --pretty=test-foo
'

test_expect_success 'NUL separation' '
	printf "add bar\0$(commit_msg)" >expected &&
	git log -z --pretty="format:%s" >actual &&
	test_cmp expected actual
'

test_expect_success 'NUL termination' '
	printf "add bar\0$(commit_msg)\0" >expected &&
	git log -z --pretty="tformat:%s" >actual &&
	test_cmp expected actual
'

test_expect_success 'NUL separation with --stat' '
	stat0_part=$(git diff --stat HEAD^ HEAD) &&
	stat1_part=$(git diff-tree --no-commit-id --stat --root HEAD^) &&
	printf "add bar\n$stat0_part\n\0$(commit_msg)\n$stat1_part\n" >expected &&
	git log -z --stat --pretty="format:%s" >actual &&
	test_i18ncmp expected actual
'

test_expect_failure 'NUL termination with --stat' '
	stat0_part=$(git diff --stat HEAD^ HEAD) &&
	stat1_part=$(git diff-tree --no-commit-id --stat --root HEAD^) &&
	printf "add bar\n$stat0_part\n\0$(commit_msg)\n$stat1_part\n0" >expected &&
	git log -z --stat --pretty="tformat:%s" >actual &&
	test_i18ncmp expected actual
'

test_expect_success 'setup more commits' '
	test_commit "message one" one one message-one &&
	test_commit "message two" two two message-two &&
	head1=$(git rev-parse --verify --short HEAD~0) &&
	head2=$(git rev-parse --verify --short HEAD~1) &&
	head3=$(git rev-parse --verify --short HEAD~2) &&
	head4=$(git rev-parse --verify --short HEAD~3)
'

test_expect_success 'left alignment formatting' '
	git log --pretty="format:%<(40)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
message two                            Z
message one                            Z
add bar                                Z
$(commit_msg)                    Z
EOF
	test_cmp expected actual
'

test_expect_success 'left alignment formatting at the nth column' '
	git log --pretty="format:%h %<|(40)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
$head1 message two                    Z
$head2 message one                    Z
$head3 add bar                        Z
$head4 $(commit_msg)            Z
EOF
	test_cmp expected actual
'

test_expect_success 'left alignment formatting with no padding' '
	git log --pretty="format:%<(1)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	cat <<EOF >expected &&
message two
message one
add bar
$(commit_msg)
EOF
	test_cmp expected actual
'

test_expect_success 'left alignment formatting with trunc' '
	git log --pretty="format:%<(10,trunc)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
message ..
message ..
add bar  Z
initial...
EOF
	test_cmp expected actual
'

test_expect_success 'left alignment formatting with ltrunc' '
	git log --pretty="format:%<(10,ltrunc)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
..sage two
..sage one
add bar  Z
..${sample_utf8_part}lich
EOF
	test_cmp expected actual
'

test_expect_success 'left alignment formatting with mtrunc' '
	git log --pretty="format:%<(10,mtrunc)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
mess.. two
mess.. one
add bar  Z
init..lich
EOF
	test_cmp expected actual
'

test_expect_success 'right alignment formatting' '
	git log --pretty="format:%>(40)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
Z                            message two
Z                            message one
Z                                add bar
Z                    $(commit_msg)
EOF
	test_cmp expected actual
'

test_expect_success 'right alignment formatting at the nth column' '
	git log --pretty="format:%h %>|(40)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
$head1                      message two
$head2                      message one
$head3                          add bar
$head4              $(commit_msg)
EOF
	test_cmp expected actual
'

test_expect_success 'right alignment formatting with no padding' '
	git log --pretty="format:%>(1)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	cat <<EOF >expected &&
message two
message one
add bar
$(commit_msg)
EOF
	test_cmp expected actual
'

test_expect_success 'center alignment formatting' '
	git log --pretty="format:%><(40)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
Z             message two              Z
Z             message one              Z
Z               add bar                Z
Z         $(commit_msg)          Z
EOF
	test_cmp expected actual
'

test_expect_success 'center alignment formatting at the nth column' '
	git log --pretty="format:%h %><|(40)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	qz_to_tab_space <<EOF >expected &&
$head1           message two          Z
$head2           message one          Z
$head3             add bar            Z
$head4       $(commit_msg)      Z
EOF
	test_cmp expected actual
'

test_expect_success 'center alignment formatting with no padding' '
	git log --pretty="format:%><(1)%s" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	cat <<EOF >expected &&
message two
message one
add bar
$(commit_msg)
EOF
	test_cmp expected actual
'

test_expect_success 'left/right alignment formatting with stealing' '
	git commit --amend -m short --author "long long long <long@me.com>" &&
	git log --pretty="format:%<(10,trunc)%s%>>(10,ltrunc)% an" >actual &&
	# complete the incomplete line at the end
	echo >>actual &&
	cat <<EOF >expected &&
short long  long long
message ..   A U Thor
add bar      A U Thor
initial...   A U Thor
EOF
	test_cmp expected actual
'

test_done
