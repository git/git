#!/bin/sh

test_description='Test ref-filter and pretty APIs for commit and tag messages using CRLF'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

LIB_CRLF_BRANCHES=""

create_crlf_ref () {
	branch="$1" &&
	cat >.crlf-orig-$branch.txt &&
	append_cr <.crlf-orig-$branch.txt >.crlf-message-$branch.txt &&
	grep 'Subject' .crlf-orig-$branch.txt | tr '\n' ' ' | sed 's/[ ]*$//' | tr -d '\n' >.crlf-subject-$branch.txt &&
	grep 'Body' .crlf-orig-$branch.txt | append_cr >.crlf-body-$branch.txt &&
	LIB_CRLF_BRANCHES="${LIB_CRLF_BRANCHES} ${branch}" &&
	test_tick &&
	hash=$(git commit-tree HEAD^{tree} -p HEAD -F .crlf-message-${branch}.txt) &&
	git branch ${branch} ${hash} &&
	git tag tag-${branch} ${branch} -F .crlf-message-${branch}.txt --cleanup=verbatim
}

create_crlf_refs () {
	create_crlf_ref crlf <<-\EOF &&
	Subject first line

	Body first line
	Body second line
	EOF
	create_crlf_ref crlf-empty-lines-after-subject <<-\EOF &&
	Subject first line


	Body first line
	Body second line
	EOF
	create_crlf_ref crlf-two-line-subject <<-\EOF &&
	Subject first line
	Subject second line

	Body first line
	Body second line
	EOF
	create_crlf_ref crlf-two-line-subject-no-body <<-\EOF &&
	Subject first line
	Subject second line
	EOF
	create_crlf_ref crlf-two-line-subject-no-body-trailing-newline <<-\EOF
	Subject first line
	Subject second line

	EOF
}

test_crlf_subject_body_and_contents() {
	command_and_args="$@" &&
	command=$1 &&
	if test ${command} = "branch" || test ${command} = "for-each-ref" || test ${command} = "tag"
	then
		atoms="(contents:subject) (contents:body) (contents)"
	elif test ${command} = "log" || test ${command} = "show"
	then
		atoms="s b B"
	fi &&
	files="subject body message" &&
	while test -n "${atoms}"
	do
		set ${atoms} && atom=$1 && shift && atoms="$*" &&
		set ${files} && file=$1 && shift && files="$*" &&
		test_expect_success "${command}: --format='%${atom}' works with messages using CRLF" "
			rm -f expect &&
			for ref in ${LIB_CRLF_BRANCHES}
			do
				cat .crlf-${file}-\"\${ref}\".txt >>expect &&
				printf \"\n\" >>expect || return 1
			done &&
			git $command_and_args --format=\"%${atom}\" >actual &&
			test_cmp expect actual
		"
	done
}


test_expect_success 'Setup refs with commit and tag messages using CRLF' '
	test_commit inital &&
	create_crlf_refs
'

test_expect_success 'branch: --verbose works with messages using CRLF' '
	rm -f expect &&
	for branch in $LIB_CRLF_BRANCHES
	do
		printf "  " >>expect &&
		cat .crlf-subject-${branch}.txt >>expect &&
		printf "\n" >>expect || return 1
	done &&
	git branch -v >tmp &&
	# Remove first two columns, and the line for the currently checked out branch
	current=$(git branch --show-current) &&
	awk "/$current/ { next } { \$1 = \$2 = \"\" } 1" <tmp >actual &&
	test_cmp expect actual
'

test_crlf_subject_body_and_contents branch --list crlf*

test_crlf_subject_body_and_contents tag --list tag-crlf*

test_crlf_subject_body_and_contents for-each-ref refs/heads/crlf*

test_expect_success 'log: --oneline works with messages using CRLF' '
	for branch in $LIB_CRLF_BRANCHES
	do
		cat .crlf-subject-${branch}.txt >expect &&
		printf "\n" >>expect &&
		git log --oneline -1 ${branch} >tmp-branch &&
		git log --oneline -1 tag-${branch} >tmp-tag &&
		cut -d" " -f2- <tmp-branch >actual-branch &&
		cut -d" " -f2- <tmp-tag >actual-tag &&
		test_cmp expect actual-branch &&
		test_cmp expect actual-tag || return 1
	done
'

test_crlf_subject_body_and_contents log --all --reverse --grep Subject

test_crlf_subject_body_and_contents show $LIB_CRLF_BRANCHES

test_done
