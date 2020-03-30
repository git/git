#!/bin/sh
#
# Copyright (c) 2020 Jiang Xin
#

test_description='Test proc-receive hook'

. ./test-lib.sh

# Create commits in <repo> and assign each commit's oid to shell variables
# given in the arguments (A, B, and C). E.g.:
#
#     create_commits_in <repo> A B C
#
# NOTE: Never calling this function from a subshell since variable
# assignments will disappear when subshell exits.
create_commits_in () {
	repo="$1" &&
	if ! parent=$(git -C "$repo" rev-parse HEAD^{})
	then
		parent=
	fi &&
	T=$(git -C "$repo" write-tree) &&
	shift &&
	while test $# -gt 0
	do
		name=$1 &&
		test_tick &&
		if test -z "$parent"
		then
			oid=$(echo $name | git -C "$repo" commit-tree $T)
		else
			oid=$(echo $name | git -C "$repo" commit-tree -p $parent $T)
		fi &&
		eval $name=$oid &&
		parent=$oid &&
		shift ||
		return 1
	done &&
	git -C "$repo" update-ref refs/heads/master $oid
}

# Format the output of git-push, git-show-ref and other commands to make a
# user-friendly and stable text.  We can easily prepare the expect text
# without having to worry about future changes of the commit ID and spaces
# of the output.  We also replce single quotes with double quotes, because
# it is boring to prepare unquoted single quotes in expect txt.
make_user_friendly_and_stable_output () {
	sed \
		-e "s/  *\$//" \
		-e "s/   */ /g" \
		-e "s/'/\"/g" \
		-e "s/$A/<COMMIT-A>/g" \
		-e "s/$B/<COMMIT-B>/g" \
		-e "s/$TAG/<TAG-v123>/g" \
		-e "s/$ZERO_OID/<ZERO-OID>/g" \
		-e "s/[0-9a-f]\{7,\}/<OID>/g"
}

# Refs of upstream : master(B)  next(A)
# Refs of workbench: master(A)           tags/v123
test_expect_success "setup" '
	git init --bare upstream &&
	git init workbench &&
	create_commits_in workbench A B &&
	(
		cd workbench &&
		# Try to make a stable fixed width for abbreviated commit ID,
		# this fixed-width oid will be replaced with "<OID>".
		git config core.abbrev 7 &&
		git remote add origin ../upstream &&
		git update-ref refs/heads/master $A &&
		git tag -m "v123" v123 $A &&
		git push origin \
			$B:refs/heads/master \
			$A:refs/heads/next
	) &&
	TAG=$(git -C workbench rev-parse v123) &&

	# setup pre-receive hook
	cat >upstream/hooks/pre-receive <<-\EOF &&
	#!/bin/sh

	echo >&2 "# pre-receive hook"

	while read old new ref
	do
		echo >&2 "pre-receive< $old $new $ref"
	done
	EOF

	# setup post-receive hook
	cat >upstream/hooks/post-receive <<-\EOF &&
	#!/bin/sh

	echo >&2 "# post-receive hook"

	while read old new ref
	do
		echo >&2 "post-receive< $old $new $ref"
	done
	EOF

	chmod a+x \
		upstream/hooks/pre-receive \
		upstream/hooks/post-receive
'

# Refs of upstream : master(B)  next(A)
# Refs of workbench: master(A)           tags/v123
# git-push -f      : master(A)  NULL     tags/v123  refs/review/master/topic(A)  a/b/c(A)
test_expect_success "normal git-push command" '
	git -C workbench push -f origin \
		refs/tags/v123 \
		:refs/heads/next \
		HEAD:refs/heads/master \
		HEAD:refs/review/master/topic \
		HEAD:refs/heads/a/b/c \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-B> <COMMIT-A> refs/heads/master
	remote: pre-receive< <COMMIT-A> <ZERO-OID> refs/heads/next
	remote: pre-receive< <ZERO-OID> <TAG-v123> refs/tags/v123
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/review/master/topic
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/a/b/c
	remote: # post-receive hook
	remote: post-receive< <COMMIT-B> <COMMIT-A> refs/heads/master
	remote: post-receive< <COMMIT-A> <ZERO-OID> refs/heads/next
	remote: post-receive< <ZERO-OID> <TAG-v123> refs/tags/v123
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/review/master/topic
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/a/b/c
	To ../upstream
	 + <OID>...<OID> HEAD -> master (forced update)
	 - [deleted] next
	 * [new tag] v123 -> v123
	 * [new reference] HEAD -> refs/review/master/topic
	 * [new branch] HEAD -> a/b/c
	EOF
	test_cmp expect actual &&
	git -C upstream show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/a/b/c
	<COMMIT-A> refs/heads/master
	<COMMIT-A> refs/review/master/topic
	<TAG-v123> refs/tags/v123
	EOF
	test_cmp expect actual
'

test_done
