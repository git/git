#!/bin/sh
#
# Copyright (c) 2010 Nazri Ramliy
#

test_description='Test for "git log --decorate" colors'

. ./test-lib.sh

get_color ()
{
	git config --get-color no.such.slot "$1"
}

test_expect_success setup '
	git config diff.color.commit yellow &&
	git config color.decorate.branch green &&
	git config color.decorate.remoteBranch red &&
	git config color.decorate.tag "reverse bold yellow" &&
	git config color.decorate.stash magenta &&
	git config color.decorate.HEAD cyan &&

	c_reset=$(get_color reset) &&

	c_commit=$(get_color yellow) &&
	c_branch=$(get_color green) &&
	c_remoteBranch=$(get_color red) &&
	c_tag=$(get_color "reverse bold yellow") &&
	c_stash=$(get_color magenta) &&
	c_HEAD=$(get_color cyan) &&

	test_commit A &&
	git clone . other &&
	(
		cd other &&
		test_commit A1
	) &&

	git remote add -f other ./other &&
	test_commit B &&
	git tag v1.0 &&
	echo >>A.t &&
	git stash save Changes to A.t
'

cat >expected <<EOF
${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_HEAD}HEAD ->\
 ${c_reset}${c_branch}master${c_reset}${c_commit},\
 ${c_reset}${c_tag}tag: v1.0${c_reset}${c_commit},\
 ${c_reset}${c_tag}tag: B${c_reset}${c_commit})${c_reset} B
${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_tag}tag: A1${c_reset}${c_commit},\
 ${c_reset}${c_remoteBranch}other/master${c_reset}${c_commit})${c_reset} A1
${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_stash}refs/stash${c_reset}${c_commit})${c_reset}\
 On master: Changes to A.t
${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_tag}tag: A${c_reset}${c_commit})${c_reset} A
EOF

# We want log to show all, but the second parent to refs/stash is irrelevant
# to this test since it does not contain any decoration, hence --first-parent
test_expect_success 'Commit Decorations Colored Correctly' '
	git log --first-parent --abbrev=10 --all --decorate --oneline --color=always |
	sed "s/[0-9a-f]\{10,10\}/COMMIT_ID/" >out &&
	test_cmp expected out
'

test_done
