#!/bin/sh

test_description='reftables are compatible with JGit'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
GIT_TEST_DEFAULT_REF_FORMAT=reftable
export GIT_TEST_DEFAULT_REF_FORMAT

# JGit does not support the 'link' DIRC extension.
GIT_TEST_SPLIT_INDEX=0
export GIT_TEST_SPLIT_INDEX

. ./test-lib.sh

if ! test_have_prereq JGIT
then
	skip_all='skipping reftable JGit tests; JGit is not present in PATH'
	test_done
fi

if ! test_have_prereq SHA1
then
	skip_all='skipping reftable JGit tests; JGit does not support SHA256 reftables'
	test_done
fi

test_commit_jgit () {
	touch "$1" &&
	jgit add "$1" &&
	jgit commit -m "$1"
}

test_same_refs () {
	git show-ref --head >cgit.actual &&
	jgit show-ref >jgit-tabs.actual &&
	tr "\t" " " <jgit-tabs.actual >jgit.actual &&
	test_cmp cgit.actual jgit.actual
}

test_same_ref () {
	git rev-parse "$1" >cgit.actual &&
	jgit rev-parse "$1" >jgit.actual &&
	test_cmp cgit.actual jgit.actual
}

test_same_reflog () {
	git reflog "$*" >cgit.actual &&
	jgit reflog "$*" >jgit-newline.actual &&
	sed '/^$/d' <jgit-newline.actual >jgit.actual &&
	test_cmp cgit.actual jgit.actual
}

test_expect_success 'CGit repository can be read by JGit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		test_same_refs &&
		test_same_ref HEAD &&
		test_same_reflog HEAD
	)
'

test_expect_success 'JGit repository can be read by CGit' '
	test_when_finished "rm -rf repo" &&
	jgit init repo &&
	(
		cd repo &&

		touch file &&
		jgit add file &&
		jgit commit -m "initial commit" &&

		# Note that we must convert the ref storage after we have
		# written the default branch. Otherwise JGit will end up with
		# no HEAD at all.
		jgit convert-ref-storage --format=reftable &&

		test_same_refs &&
		test_same_ref HEAD &&
		# Interestingly, JGit cannot read its own reflog here. CGit can
		# though.
		printf "%s HEAD@{0}: commit (initial): initial commit" "$(git rev-parse --short HEAD)" >expect &&
		git reflog HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'mixed writes from JGit and CGit' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit A &&
		test_commit_jgit B &&
		test_commit C &&
		test_commit_jgit D &&

		test_same_refs &&
		test_same_ref HEAD &&
		test_same_reflog HEAD
	)
'

test_expect_success 'JGit can read multi-level index' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit A &&
		awk "
		    BEGIN {
			print \"start\";
			for (i = 0; i < 10000; i++)
			    printf \"create refs/heads/branch-%d HEAD\n\", i;
			print \"commit\";
		    }
		" >input &&
		git update-ref --stdin <input &&

		test_same_refs &&
		test_same_ref refs/heads/branch-1 &&
		test_same_ref refs/heads/branch-5738 &&
		test_same_ref refs/heads/branch-9999
	)
'

test_done
