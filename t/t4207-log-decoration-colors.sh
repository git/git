#!/bin/sh
#
# Copyright (c) 2010 Nazri Ramliy
#

test_description='test "git log --decorate" colors'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success setup '
	git config diff.color.commit yellow &&
	git config color.decorate.branch green &&
	git config color.decorate.remoteBranch red &&
	git config color.decorate.tag "reverse bold yellow" &&
	git config color.decorate.stash magenta &&
	git config color.decorate.grafted black &&
	git config color.decorate.HEAD cyan &&

	c_reset="<RESET>" &&

	c_commit="<YELLOW>" &&
	c_branch="<GREEN>" &&
	c_remoteBranch="<RED>" &&
	c_tag="<BOLD;REVERSE;YELLOW>" &&
	c_stash="<MAGENTA>" &&
	c_HEAD="<CYAN>" &&
	c_grafted="<BLACK>" &&

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

cmp_filtered_decorations () {
	sed "s/$OID_REGEX/COMMIT_ID/" actual | test_decode_color >filtered &&
	test_cmp expect filtered
}

# We want log to show all, but the second parent to refs/stash is irrelevant
# to this test since it does not contain any decoration, hence --first-parent
test_expect_success 'commit decorations colored correctly' '
	cat >expect <<-EOF &&
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_HEAD}HEAD -> \
${c_reset}${c_branch}main${c_reset}${c_commit}, \
${c_reset}${c_tag}tag: v1.0${c_reset}${c_commit}, \
${c_reset}${c_tag}tag: B${c_reset}${c_commit})${c_reset} B
${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_tag}tag: A1${c_reset}${c_commit}, \
${c_reset}${c_remoteBranch}other/main${c_reset}${c_commit})${c_reset} A1
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_stash}refs/stash${c_reset}${c_commit})${c_reset} \
On main: Changes to A.t
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_tag}tag: A${c_reset}${c_commit})${c_reset} A
	EOF

	git log --first-parent --no-abbrev --decorate --oneline --color=always --all >actual &&
	cmp_filtered_decorations
'

test_expect_success 'test coloring with replace-objects' '
	test_when_finished rm -rf .git/refs/replace* &&
	test_commit C &&
	test_commit D &&

	git replace HEAD~1 HEAD~2 &&

	cat >expect <<-EOF &&
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_HEAD}HEAD -> \
${c_reset}${c_branch}main${c_reset}${c_commit}, \
${c_reset}${c_tag}tag: D${c_reset}${c_commit})${c_reset} D
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_tag}tag: C${c_reset}${c_commit}, \
${c_reset}${c_grafted}replaced${c_reset}${c_commit})${c_reset} B
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_tag}tag: A${c_reset}${c_commit})${c_reset} A
EOF

	git log --first-parent --no-abbrev --decorate --oneline --color=always HEAD >actual &&
	cmp_filtered_decorations &&
	git replace -d HEAD~1 &&

	GIT_REPLACE_REF_BASE=refs/replace2/ git replace HEAD~1 HEAD~2 &&
	GIT_REPLACE_REF_BASE=refs/replace2/ git log --first-parent \
		--no-abbrev --decorate --oneline --color=always HEAD >actual &&
	cmp_filtered_decorations
'

test_expect_success 'test coloring with grafted commit' '
	test_when_finished rm -rf .git/refs/replace* &&

	git replace --graft HEAD HEAD~2 &&

	cat >expect <<-EOF &&
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_HEAD}HEAD -> \
${c_reset}${c_branch}main${c_reset}${c_commit}, \
${c_reset}${c_tag}tag: D${c_reset}${c_commit}, \
${c_reset}${c_grafted}replaced${c_reset}${c_commit})${c_reset} D
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_tag}tag: v1.0${c_reset}${c_commit}, \
${c_reset}${c_tag}tag: B${c_reset}${c_commit})${c_reset} B
	${c_commit}COMMIT_ID${c_reset}${c_commit} (${c_reset}${c_tag}tag: A${c_reset}${c_commit})${c_reset} A
	EOF

	git log --first-parent --no-abbrev --decorate --oneline --color=always HEAD >actual &&
	cmp_filtered_decorations &&
	git replace -d HEAD &&

	GIT_REPLACE_REF_BASE=refs/replace2/ git replace --graft HEAD HEAD~2 &&
	GIT_REPLACE_REF_BASE=refs/replace2/ git log --first-parent \
		--no-abbrev --decorate --oneline --color=always HEAD >actual &&
	cmp_filtered_decorations
'

test_done
