#!/bin/sh
#
# Copyright (c) 2020 Google LLC
#

test_description='reftable basics'

. ./test-lib.sh

INVALID_SHA1=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main

initialize ()  {
	# remove the repo that the framework provides, because it's not using reftable.
	rm -rf .git &&
	GIT_TEST_REFTABLE=1 git init
}

write_script fake_editor <<\EOF
echo "$MSG" >"$1"
echo "$MSG" >&2
EOF
GIT_EDITOR=./fake_editor
export GIT_EDITOR


test_expect_success 'using reftable' '
	initialize &&
	test -d .git/reftable &&
	test -f .git/reftable/tables.list
'

test_expect_success 'read existing old OID if REF_HAVE_OLD is not set' '
	initialize &&
	test_commit 1st &&
	test_commit 2nd &&
	MSG=b4 git notes add &&
	MSG=b3 git notes edit  &&
	echo b4 >expect &&
	git notes --ref commits@{1} show >actual &&
	test_cmp expect actual
'

test_expect_success 'git reflog delete' '
	initialize &&
	test_commit file &&
	test_commit file2 &&
	test_commit file3 &&
	test_commit file4 &&
	git reflog delete HEAD@{1} &&
	git reflog >output &&
	! grep file3 output
'

test_expect_success 'branch -D delete nonexistent branch' '
	initialize &&
	test_commit file &&
	test_must_fail git branch -D ../../my-private-file
'

test_expect_success 'branch copy' '
	initialize &&
	test_commit file1 &&
	test_commit file2 &&
	git branch src &&
	git reflog src >expect &&
	git branch -c src dst &&
	git reflog dst | sed "s/dst/src/g" >actual &&
	test_cmp expect actual
'

test_expect_success 'update-ref on corrupted data' '
	initialize &&
	test_commit file1 &&
	OLD_SHA1=$(git rev-parse HEAD) &&
	test_commit file2 &&
	ls -l .git/reftable &&
	for f in .git/reftable/*.ref
	do
		>$f
	done &&
	test_must_fail git update-ref refs/heads/main $OLD_SHA1
'

test_expect_success 'git stash' '
	initialize &&
	test_commit file &&
	touch actual expected &&
	git -c status.showStash=true status >expected &&
	echo hoi >>file.t &&
	git stash push -m stashed &&
	git stash clear &&
	git -c status.showStash=true status >actual &&
	test_cmp expected actual
'

test_expect_success 'rename branch' '
	initialize &&
	git symbolic-ref HEAD refs/heads/before &&
	test_commit file &&
	git show-ref >show-ref.output &&
	sed s/before/after/g <show-ref.output >expected &&
	git branch -M after &&
	git show-ref >actual &&
	test_cmp expected actual
'

test_expect_success 'SHA256 support, env' '
	GIT_DEFAULT_HASH=sha256 && export GIT_DEFAULT_HASH &&
	initialize &&
	test_commit file &&
	git rev-parse HEAD >head_commit &&
	tr "[a-f0-9]" "x" < head_commit >actual &&
	echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx >expected &&
	test_cmp expected actual &&
	test -f .git/reftable/tables.list
'

test_expect_success 'SHA256 support, option' '
	rm -rf .git &&
	GIT_TEST_REFTABLE=1 git init --object-format=sha256 &&
	test_commit file &&
	git rev-parse HEAD >head_commit &&
	tr "[a-f0-9]" "x" < head_commit >actual &&
	echo xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx >expected &&
	test_cmp expected actual &&
	test -f .git/reftable/tables.list
'

test_expect_success 'delete ref' '
	initialize &&
	test_commit file &&
	SHA=$(git show-ref -s --verify HEAD) &&
	test_write_lines "$SHA refs/heads/main" "$SHA refs/tags/file" >expect &&
	git show-ref >actual &&
	test_must_fail git update-ref -d refs/tags/file $INVALID_SHA1 &&
	test_cmp expect actual &&
	git update-ref -d refs/tags/file $SHA  &&
	test_write_lines "$SHA refs/heads/main" >expect &&
	git show-ref >actual &&
	test_cmp expect actual
'


test_expect_success 'clone calls transaction_initial_commit' '
	test_commit message1 file1 &&
	git clone . cloned &&
	(test  -f cloned/file1 || echo "Fixme.")
'

test_expect_success 'basic operation of reftable storage: commit, show-ref' '
	initialize &&
	test_commit file &&
	test_write_lines refs/heads/main refs/tags/file >expect &&
	git show-ref > show-ref.output &&
	cut -f2 -d" " < show-ref.output >actual &&
	test_cmp actual expect
'

test_expect_success 'reflog, repack' '
	initialize &&
	for count in $(test_seq 1 10)
	do
		test_commit "number $count" file.t $count number-$count ||
		return 1
	done &&
	git pack-refs &&
	ls -1 .git/reftable >table-files &&
	test_line_count = 2 table-files &&
	git reflog refs/heads/main >output &&
	test_line_count = 10 output &&
	grep "commit (initial): number 1" output &&
	grep "commit: number 10" output &&
	git gc &&
	git reflog refs/heads/main >output &&
	test_line_count = 0 output
'

test_expect_success 'branch switch in reflog output' '
	initialize &&
	test_commit file1 &&
	git checkout -b branch1 &&
	test_commit file2 &&
	git checkout -b branch2 &&
	git switch - &&
	git rev-parse --symbolic-full-name HEAD >actual &&
	echo refs/heads/branch1 >expect &&
	test_cmp actual expect
'


# This matches show-ref's output
print_ref() {
	echo "$(git rev-parse "$1") $1"
}

test_expect_success 'peeled tags are stored' '
	initialize &&
	test_commit file &&
	git tag -m "annotated tag" test_tag HEAD &&
	{
		print_ref "refs/heads/main" &&
		print_ref "refs/tags/file" &&
		print_ref "refs/tags/test_tag" &&
		print_ref "refs/tags/test_tag^{}"
	} >expect &&
	git show-ref -d >actual &&
	test_cmp expect actual
'

test_expect_success 'show-ref works on fresh repo' '
	rm -rf .git &&
	GIT_TEST_REFTABLE=1 git init &&
	>expect &&
	test_must_fail git show-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'checkout unborn branch' '
	initialize &&
	git checkout -b main
'


test_expect_success 'dir/file conflict' '
	initialize &&
	test_commit file &&
	test_must_fail git branch main/forbidden
'


test_expect_success 'do not clobber existing repo' '
	rm -rf .git &&
	git init &&
	cat .git/HEAD >expect &&
	test_commit file &&
	(GIT_TEST_REFTABLE=1 git init || true) &&
	cat .git/HEAD >actual &&
	test_cmp expect actual
'

# cherry-pick uses a pseudo ref.
test_expect_success 'pseudo refs' '
	initialize &&
	test_commit message1 file1 &&
	test_commit message2 file2 &&
	git branch source &&
	git checkout HEAD^ &&
	test_commit message3 file3 &&
	git cherry-pick source &&
	test -f file2
'

# cherry-pick uses a pseudo ref.
test_expect_success 'rebase' '
	initialize &&
	test_commit message1 file1 &&
	test_commit message2 file2 &&
	git branch source &&
	git checkout HEAD^ &&
	test_commit message3 file3 &&
	git rebase source &&
	test -f file2
'

test_expect_success 'worktrees' '
	(GIT_TEST_REFTABLE=1 git init start) &&
	(cd start && test_commit file1 && git checkout -b branch1 &&
	git checkout -b branch2 &&
	git worktree add  ../wt
	) &&
	cd wt &&
	git checkout branch1 &&
	git branch
'

test_expect_success 'worktrees 2' '
	initialize &&
	test_commit file1 &&
	mkdir existing_empty &&
	git worktree add --detach existing_empty main
'

test_expect_success 'FETCH_HEAD' '
	initialize &&
	test_commit one &&
	(git init sub && cd sub && test_commit two) &&
	git --git-dir sub/.git rev-parse HEAD >expect &&
	git fetch sub &&
	git checkout FETCH_HEAD &&
	git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'dump reftable' '
	initialize &&
	hash_id=$(git config extensions.objectformat) &&
	test-tool dump-reftable $(test "${hash_id}" = "sha256" && echo "-6") -s .git/reftable
'

test_done
