#!/bin/sh
#
# Copyright (c) 2010 Nazri Ramliy
#

test_description='Test for "but log --decorate" colors'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	but config diff.color.cummit yellow &&
	but config color.decorate.branch green &&
	but config color.decorate.remoteBranch red &&
	but config color.decorate.tag "reverse bold yellow" &&
	but config color.decorate.stash magenta &&
	but config color.decorate.HEAD cyan &&

	c_reset="<RESET>" &&

	c_cummit="<YELLOW>" &&
	c_branch="<GREEN>" &&
	c_remoteBranch="<RED>" &&
	c_tag="<BOLD;REVERSE;YELLOW>" &&
	c_stash="<MAGENTA>" &&
	c_HEAD="<CYAN>" &&

	test_cummit A &&
	but clone . other &&
	(
		cd other &&
		test_cummit A1
	) &&

	but remote add -f other ./other &&
	test_cummit B &&
	but tag v1.0 &&
	echo >>A.t &&
	but stash save Changes to A.t
'

cat >expected <<EOF
${c_cummit}CUMMIT_ID${c_reset}${c_cummit} (${c_reset}${c_HEAD}HEAD ->\
 ${c_reset}${c_branch}main${c_reset}${c_cummit},\
 ${c_reset}${c_tag}tag: v1.0${c_reset}${c_cummit},\
 ${c_reset}${c_tag}tag: B${c_reset}${c_cummit})${c_reset} B
${c_cummit}CUMMIT_ID${c_reset}${c_cummit} (${c_reset}${c_tag}tag: A1${c_reset}${c_cummit},\
 ${c_reset}${c_remoteBranch}other/main${c_reset}${c_cummit})${c_reset} A1
${c_cummit}CUMMIT_ID${c_reset}${c_cummit} (${c_reset}${c_stash}refs/stash${c_reset}${c_cummit})${c_reset}\
 On main: Changes to A.t
${c_cummit}CUMMIT_ID${c_reset}${c_cummit} (${c_reset}${c_tag}tag: A${c_reset}${c_cummit})${c_reset} A
EOF

# We want log to show all, but the second parent to refs/stash is irrelevant
# to this test since it does not contain any decoration, hence --first-parent
test_expect_success 'cummit Decorations Colored Correctly' '
	but log --first-parent --abbrev=10 --all --decorate --oneline --color=always |
	sed "s/[0-9a-f]\{10,10\}/CUMMIT_ID/" |
	test_decode_color >out &&
	test_cmp expected out
'

test_done
