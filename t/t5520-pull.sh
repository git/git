#!/bin/sh

test_description='pulling into void'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

modify () {
	sed -e "$1" "$2" >"$2.x" &&
	mv "$2.x" "$2"
}

test_pull_autostash () {
	expect_parent_num="$1" &&
	shift &&
	git reset --hard before-rebase &&
	echo dirty >new_file &&
	git add new_file &&
	git pull "$@" . copy &&
	test_cmp_rev HEAD^"$expect_parent_num" copy &&
	echo dirty >expect &&
	test_cmp expect new_file &&
	echo "modified again" >expect &&
	test_cmp expect file
}

test_pull_autostash_fail () {
	git reset --hard before-rebase &&
	echo dirty >new_file &&
	git add new_file &&
	test_must_fail git pull "$@" . copy 2>err &&
	test_grep -E "uncommitted changes.|overwritten by merge:" err
}

test_expect_success setup '
	echo file >file &&
	git add file &&
	git commit -a -m original
'

test_expect_success 'pulling into void' '
	git init cloned &&
	(
		cd cloned &&
		git pull ..
	) &&
	test_path_is_file file &&
	test_path_is_file cloned/file &&
	test_cmp file cloned/file
'

test_expect_success 'pulling into void using main:main' '
	git init cloned-uho &&
	(
		cd cloned-uho &&
		git pull .. main:main
	) &&
	test_path_is_file file &&
	test_path_is_file cloned-uho/file &&
	test_cmp file cloned-uho/file
'

test_expect_success 'pulling into void does not overwrite untracked files' '
	git init cloned-untracked &&
	(
		cd cloned-untracked &&
		echo untracked >file &&
		test_must_fail git pull .. main &&
		echo untracked >expect &&
		test_cmp expect file
	)
'

test_expect_success 'pulling into void does not overwrite staged files' '
	git init cloned-staged-colliding &&
	(
		cd cloned-staged-colliding &&
		echo "alternate content" >file &&
		git add file &&
		test_must_fail git pull .. main &&
		echo "alternate content" >expect &&
		test_cmp expect file &&
		git cat-file blob :file >file.index &&
		test_cmp expect file.index
	)
'

test_expect_success 'pulling into void does not remove new staged files' '
	git init cloned-staged-new &&
	(
		cd cloned-staged-new &&
		echo "new tracked file" >newfile &&
		git add newfile &&
		git pull .. main &&
		echo "new tracked file" >expect &&
		test_cmp expect newfile &&
		git cat-file blob :newfile >newfile.index &&
		test_cmp expect newfile.index
	)
'

test_expect_success 'pulling into void must not create an octopus' '
	git init cloned-octopus &&
	(
		cd cloned-octopus &&
		test_must_fail git pull .. main main &&
		test_path_is_missing file
	)
'

test_expect_success 'test . as a remote' '
	git branch copy main &&
	git config branch.copy.remote . &&
	git config branch.copy.merge refs/heads/main &&
	echo updated >file &&
	git commit -a -m updated &&
	git checkout copy &&
	echo file >expect &&
	test_cmp expect file &&
	git pull &&
	echo updated >expect &&
	test_cmp expect file &&
	git reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success 'the default remote . should not break explicit pull' '
	git checkout -b second main^ &&
	echo modified >file &&
	git commit -a -m modified &&
	git checkout copy &&
	git reset --hard HEAD^ &&
	echo file >expect &&
	test_cmp expect file &&
	git pull --no-rebase . second &&
	echo modified >expect &&
	test_cmp expect file &&
	git reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull --no-rebase . second: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success 'fail if wildcard spec does not match any refs' '
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail git pull . "refs/nonexisting1/*:refs/nonexisting2/*" 2>err &&
	test_grep "no candidates for merging" err &&
	test_cmp expect file
'

test_expect_success 'fail if no branches specified with non-default remote' '
	git remote add test_remote . &&
	test_when_finished "git remote remove test_remote" &&
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	echo file >expect &&
	test_cmp expect file &&
	test_config branch.test.remote origin &&
	test_must_fail git pull test_remote 2>err &&
	test_grep "specify a branch on the command line" err &&
	test_cmp expect file
'

test_expect_success 'fail if not on a branch' '
	git remote add origin . &&
	test_when_finished "git remote remove origin" &&
	git checkout HEAD^ &&
	test_when_finished "git checkout -f copy" &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail git pull 2>err &&
	test_grep "not currently on a branch" err &&
	test_cmp expect file
'

test_expect_success 'fail if no configuration for current branch' '
	git remote add test_remote . &&
	test_when_finished "git remote remove test_remote" &&
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	test_config branch.test.remote test_remote &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail git pull 2>err &&
	test_grep "no tracking information" err &&
	test_cmp expect file
'

test_expect_success 'pull --all: fail if no configuration for current branch' '
	git remote add test_remote . &&
	test_when_finished "git remote remove test_remote" &&
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	test_config branch.test.remote test_remote &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail git pull --all 2>err &&
	test_grep "There is no tracking information" err &&
	test_cmp expect file
'

test_expect_success 'fail if upstream branch does not exist' '
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	test_config branch.test.remote . &&
	test_config branch.test.merge refs/heads/nonexisting &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail git pull 2>err &&
	test_grep "no such ref was fetched" err &&
	test_cmp expect file
'

test_expect_success 'fetch upstream branch even if refspec excludes it' '
	# the branch names are not important here except that
	# the first one must not be a prefix of the second,
	# since otherwise the ref-prefix protocol extension
	# would match both
	git branch in-refspec HEAD^ &&
	git branch not-in-refspec HEAD &&
	git init -b in-refspec downstream &&
	git -C downstream remote add -t in-refspec origin "file://$(pwd)/.git" &&
	git -C downstream config branch.in-refspec.remote origin &&
	git -C downstream config branch.in-refspec.merge refs/heads/not-in-refspec &&
	git -C downstream pull &&
	git rev-parse --verify not-in-refspec >expect &&
	git -C downstream rev-parse --verify HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'fail if the index has unresolved entries' '
	git checkout -b third second^ &&
	test_when_finished "git checkout -f copy && git branch -D third" &&
	echo file >expect &&
	test_cmp expect file &&
	test_commit modified2 file &&
	git ls-files -u >unmerged &&
	test_must_be_empty unmerged &&
	test_must_fail git pull --no-rebase . second &&
	git ls-files -u >unmerged &&
	test_file_not_empty unmerged &&
	cp file expected &&
	test_must_fail git pull . second 2>err &&
	test_grep "Pulling is not possible because you have unmerged files." err &&
	test_cmp expected file &&
	git add file &&
	git ls-files -u >unmerged &&
	test_must_be_empty unmerged &&
	test_must_fail git pull . second 2>err &&
	test_grep "You have not concluded your merge" err &&
	test_cmp expected file
'

test_expect_success 'fast-forwards working tree if branch head is updated' '
	git checkout -b third second^ &&
	test_when_finished "git checkout -f copy && git branch -D third" &&
	echo file >expect &&
	test_cmp expect file &&
	git pull . second:third 2>err &&
	test_grep "fetch updated the current branch head" err &&
	echo modified >expect &&
	test_cmp expect file &&
	test_cmp_rev third second
'

test_expect_success 'fast-forward fails with conflicting work tree' '
	git checkout -b third second^ &&
	test_when_finished "git checkout -f copy && git branch -D third" &&
	echo file >expect &&
	test_cmp expect file &&
	echo conflict >file &&
	test_must_fail git pull . second:third 2>err &&
	test_grep "Cannot fast-forward your working tree" err &&
	echo conflict >expect &&
	test_cmp expect file &&
	test_cmp_rev third second
'

test_expect_success '--rebase' '
	git branch to-rebase &&
	echo modified again >file &&
	git commit -m file file &&
	git checkout to-rebase &&
	echo new >file2 &&
	git add file2 &&
	git commit -m "new file" &&
	git tag before-rebase &&
	git pull --rebase . copy &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	git show HEAD:file2 >actual &&
	test_cmp expect actual
'

test_expect_success '--rebase (merge) fast forward' '
	git reset --hard before-rebase &&
	git checkout -b ff &&
	echo another modification >file &&
	git commit -m third file &&

	git checkout to-rebase &&
	git -c rebase.backend=merge pull --rebase . ff &&
	test_cmp_rev HEAD ff &&

	# The above only validates the result.  Did we actually bypass rebase?
	git reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull --rebase . ff: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success '--rebase (am) fast forward' '
	git reset --hard before-rebase &&

	git -c rebase.backend=apply pull --rebase . ff &&
	test_cmp_rev HEAD ff &&

	# The above only validates the result.  Did we actually bypass rebase?
	git reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull --rebase . ff: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success '--rebase --autostash fast forward' '
	test_when_finished "
		git reset --hard
		git checkout to-rebase
		git branch -D to-rebase-ff
		git branch -D behind" &&
	git branch behind &&
	git checkout -b to-rebase-ff &&
	echo another modification >>file &&
	git add file &&
	git commit -m mod &&

	git checkout behind &&
	echo dirty >file &&
	git pull --rebase --autostash . to-rebase-ff &&
	test_cmp_rev HEAD to-rebase-ff
'

test_expect_success '--rebase with rebase.autostash succeeds on ff' '
	test_when_finished "rm -fr src dst actual" &&
	git init src &&
	test_commit -C src "initial" file "content" &&
	git clone src dst &&
	test_commit -C src --printf "more_content" file "more content\ncontent\n" &&
	echo "dirty" >>dst/file &&
	test_config -C dst rebase.autostash true &&
	git -C dst pull --rebase >actual 2>&1 &&
	grep -q "Fast-forward" actual &&
	grep -q "Applied autostash." actual
'

test_expect_success '--rebase with conflicts shows advice' '
	test_when_finished "git rebase --abort; git checkout -f to-rebase" &&
	git checkout -b seq &&
	test_seq 5 >seq.txt &&
	git add seq.txt &&
	test_tick &&
	git commit -m "Add seq.txt" &&
	echo 6 >>seq.txt &&
	test_tick &&
	git commit -m "Append to seq.txt" seq.txt &&
	git checkout -b with-conflicts HEAD^ &&
	echo conflicting >>seq.txt &&
	test_tick &&
	git commit -m "Create conflict" seq.txt &&
	test_must_fail git pull --rebase . seq 2>err >out &&
	test_grep "Resolve all conflicts manually" err
'

test_expect_success 'failed --rebase shows advice' '
	test_when_finished "git rebase --abort; git checkout -f to-rebase" &&
	git checkout -b diverging &&
	test_commit attributes .gitattributes "* text=auto" attrs &&
	sha1="$(printf "1\\r\\n" | git hash-object -w --stdin)" &&
	git update-index --cacheinfo 0644 $sha1 file &&
	git commit -m v1-with-cr &&
	# force checkout because `git reset --hard` will not leave clean `file`
	git checkout -f -b fails-to-rebase HEAD^ &&
	test_commit v2-without-cr file "2" file2-lf &&
	test_must_fail git pull --rebase . diverging 2>err >out &&
	test_grep "Resolve all conflicts manually" err
'

test_expect_success '--rebase fails with multiple branches' '
	git reset --hard before-rebase &&
	test_must_fail git pull --rebase . copy main 2>err &&
	test_cmp_rev HEAD before-rebase &&
	test_grep "Cannot rebase onto multiple branches" err &&
	echo modified >expect &&
	git show HEAD:file >actual &&
	test_cmp expect actual
'

test_expect_success 'pull --rebase succeeds with dirty working directory and rebase.autostash set' '
	test_config rebase.autostash true &&
	test_pull_autostash 1 --rebase
'

test_expect_success 'pull --rebase --autostash & rebase.autostash=true' '
	test_config rebase.autostash true &&
	test_pull_autostash 1 --rebase --autostash
'

test_expect_success 'pull --rebase --autostash & rebase.autostash=false' '
	test_config rebase.autostash false &&
	test_pull_autostash 1 --rebase --autostash
'

test_expect_success 'pull --rebase --autostash & rebase.autostash unset' '
	test_unconfig rebase.autostash &&
	test_pull_autostash 1 --rebase --autostash
'

test_expect_success 'pull --rebase --no-autostash & rebase.autostash=true' '
	test_config rebase.autostash true &&
	test_pull_autostash_fail --rebase --no-autostash
'

test_expect_success 'pull --rebase --no-autostash & rebase.autostash=false' '
	test_config rebase.autostash false &&
	test_pull_autostash_fail --rebase --no-autostash
'

test_expect_success 'pull --rebase --no-autostash & rebase.autostash unset' '
	test_unconfig rebase.autostash &&
	test_pull_autostash_fail --rebase --no-autostash
'

test_expect_success 'pull succeeds with dirty working directory and merge.autostash set' '
	test_config merge.autostash true &&
	test_pull_autostash 2 --no-rebase
'

test_expect_success 'pull --autostash & merge.autostash=true' '
	test_config merge.autostash true &&
	test_pull_autostash 2 --autostash --no-rebase
'

test_expect_success 'pull --autostash & merge.autostash=false' '
	test_config merge.autostash false &&
	test_pull_autostash 2 --autostash --no-rebase
'

test_expect_success 'pull --autostash & merge.autostash unset' '
	test_unconfig merge.autostash &&
	test_pull_autostash 2 --autostash --no-rebase
'

test_expect_success 'pull --no-autostash & merge.autostash=true' '
	test_config merge.autostash true &&
	test_pull_autostash_fail --no-autostash --no-rebase
'

test_expect_success 'pull --no-autostash & merge.autostash=false' '
	test_config merge.autostash false &&
	test_pull_autostash_fail --no-autostash --no-rebase
'

test_expect_success 'pull --no-autostash & merge.autostash unset' '
	test_unconfig merge.autostash &&
	test_pull_autostash_fail --no-autostash --no-rebase
'

test_expect_success 'pull.rebase' '
	git reset --hard before-rebase &&
	test_config pull.rebase true &&
	git pull . copy &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	git show HEAD:file2 >actual &&
	test_cmp expect actual
'

test_expect_success 'pull --autostash & pull.rebase=true' '
	test_config pull.rebase true &&
	test_pull_autostash 1 --autostash
'

test_expect_success 'pull --no-autostash & pull.rebase=true' '
	test_config pull.rebase true &&
	test_pull_autostash_fail --no-autostash
'

test_expect_success 'branch.to-rebase.rebase' '
	git reset --hard before-rebase &&
	test_config branch.to-rebase.rebase true &&
	git pull . copy &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	git show HEAD:file2 >actual &&
	test_cmp expect actual
'

test_expect_success 'branch.to-rebase.rebase should override pull.rebase' '
	git reset --hard before-rebase &&
	test_config pull.rebase true &&
	test_config branch.to-rebase.rebase false &&
	git pull . copy &&
	test_cmp_rev ! HEAD^ copy &&
	echo new >expect &&
	git show HEAD:file2 >actual &&
	test_cmp expect actual
'

test_expect_success 'pull --rebase warns on --verify-signatures' '
	git reset --hard before-rebase &&
	git pull --rebase --verify-signatures . copy 2>err &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	git show HEAD:file2 >actual &&
	test_cmp expect actual &&
	test_grep "ignoring --verify-signatures for rebase" err
'

test_expect_success 'pull --rebase does not warn on --no-verify-signatures' '
	git reset --hard before-rebase &&
	git pull --rebase --no-verify-signatures . copy 2>err &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	git show HEAD:file2 >actual &&
	test_cmp expect actual &&
	test_grep ! "verify-signatures" err
'

# add a feature branch, keep-merge, that is merged into main, so the
# test can try preserving the merge commit (or not) with various
# --rebase flags/pull.rebase settings.
test_expect_success 'preserve merge setup' '
	git reset --hard before-rebase &&
	git checkout -b keep-merge second^ &&
	test_commit file3 &&
	git checkout to-rebase &&
	git merge keep-merge &&
	git tag before-preserve-rebase
'

test_expect_success 'pull.rebase=false create a new merge commit' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase false &&
	git pull . copy &&
	test_cmp_rev HEAD^1 before-preserve-rebase &&
	test_cmp_rev HEAD^2 copy &&
	echo file3 >expect &&
	git show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success 'pull.rebase=true flattens keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase true &&
	git pull . copy &&
	test_cmp_rev HEAD^^ copy &&
	echo file3 >expect &&
	git show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success 'pull.rebase=1 is treated as true and flattens keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase 1 &&
	git pull . copy &&
	test_cmp_rev HEAD^^ copy &&
	echo file3 >expect &&
	git show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success 'pull.rebase=interactive' '
	write_script "$TRASH_DIRECTORY/fake-editor" <<-\EOF &&
	echo I was here >fake.out &&
	false
	EOF
	test_set_editor "$TRASH_DIRECTORY/fake-editor" &&
	test_when_finished "test_might_fail git rebase --abort" &&
	test_must_fail git pull --rebase=interactive . copy &&
	echo "I was here" >expect &&
	test_cmp expect fake.out
'

test_expect_success 'pull --rebase=i' '
	write_script "$TRASH_DIRECTORY/fake-editor" <<-\EOF &&
	echo I was here, too >fake.out &&
	false
	EOF
	test_set_editor "$TRASH_DIRECTORY/fake-editor" &&
	test_when_finished "test_might_fail git rebase --abort" &&
	test_must_fail git pull --rebase=i . copy &&
	echo "I was here, too" >expect &&
	test_cmp expect fake.out
'

test_expect_success 'pull.rebase=invalid fails' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase invalid &&
	test_must_fail git pull . copy
'

test_expect_success '--rebase=false create a new merge commit' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase true &&
	git pull --rebase=false . copy &&
	test_cmp_rev HEAD^1 before-preserve-rebase &&
	test_cmp_rev HEAD^2 copy &&
	echo file3 >expect &&
	git show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success '--rebase=true rebases and flattens keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase merges &&
	git pull --rebase=true . copy &&
	test_cmp_rev HEAD^^ copy &&
	echo file3 >expect &&
	git show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success '--rebase=invalid fails' '
	git reset --hard before-preserve-rebase &&
	test_must_fail git pull --rebase=invalid . copy
'

test_expect_success '--rebase overrides pull.rebase=merges and flattens keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase merges &&
	git pull --rebase . copy &&
	test_cmp_rev HEAD^^ copy &&
	echo file3 >expect &&
	git show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success '--rebase with rebased upstream' '
	git remote add -f me . &&
	git checkout copy &&
	git tag copy-orig &&
	git reset --hard HEAD^ &&
	echo conflicting modification >file &&
	git commit -m conflict file &&
	git checkout to-rebase &&
	echo file >file2 &&
	git commit -m to-rebase file2 &&
	git tag to-rebase-orig &&
	git pull --rebase me copy &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2
'

test_expect_success '--rebase -f with rebased upstream' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git reset --hard to-rebase-orig &&
	git pull --rebase -f me copy &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2
'

test_expect_success '--rebase with rebased default upstream' '
	git update-ref refs/remotes/me/copy copy-orig &&
	git checkout --track -b to-rebase2 me/copy &&
	git reset --hard to-rebase-orig &&
	git pull --rebase &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2
'

test_expect_success 'rebased upstream + fetch + pull --rebase' '

	git update-ref refs/remotes/me/copy copy-orig &&
	git reset --hard to-rebase-orig &&
	git checkout --track -b to-rebase3 me/copy &&
	git reset --hard to-rebase-orig &&
	git fetch &&
	git pull --rebase &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2

'

test_expect_success 'pull --rebase dies early with dirty working directory' '
	git checkout to-rebase &&
	git update-ref refs/remotes/me/copy copy^ &&
	COPY="$(git rev-parse --verify me/copy)" &&
	git rebase --onto $COPY copy &&
	test_config branch.to-rebase.remote me &&
	test_config branch.to-rebase.merge refs/heads/copy &&
	test_config branch.to-rebase.rebase true &&
	echo dirty >>file &&
	git add file &&
	test_must_fail git pull &&
	test_cmp_rev "$COPY" me/copy &&
	git checkout HEAD -- file &&
	git pull &&
	test_cmp_rev ! "$COPY" me/copy
'

test_expect_success 'pull --rebase works on branch yet to be born' '
	git rev-parse main >expect &&
	mkdir empty_repo &&
	(
		cd empty_repo &&
		git init &&
		git pull --rebase .. main &&
		git rev-parse HEAD >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'pull --rebase fails on unborn branch with staged changes' '
	test_when_finished "rm -rf empty_repo2" &&
	git init empty_repo2 &&
	(
		cd empty_repo2 &&
		echo staged-file >staged-file &&
		git add staged-file &&
		echo staged-file >expect &&
		git ls-files >actual &&
		test_cmp expect actual &&
		test_must_fail git pull --rebase .. main 2>err &&
		git ls-files >actual &&
		test_cmp expect actual &&
		git show :staged-file >actual &&
		test_cmp expect actual &&
		test_grep "unborn branch with changes added to the index" err
	)
'

test_expect_success 'pull --rebase fails on corrupt HEAD' '
	test_when_finished "rm -rf corrupt" &&
	git init corrupt &&
	(
		cd corrupt &&
		test_commit one &&
		git rev-parse --verify HEAD >head &&
		obj=$(sed "s#^..#&/#" head) &&
		rm -f .git/objects/$obj &&
		test_must_fail git pull --rebase
	)
'

test_expect_success 'setup for detecting upstreamed changes' '
	test_create_repo src &&
	test_commit -C src --printf one stuff "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" &&
	git clone src dst &&
	(
		cd src &&
		modify s/5/43/ stuff &&
		git commit -a -m "5->43" &&
		modify s/6/42/ stuff &&
		git commit -a -m "Make it bigger"
	) &&
	(
		cd dst &&
		modify s/5/43/ stuff &&
		git commit -a -m "Independent discovery of 5->43"
	)
'

test_expect_success 'git pull --rebase detects upstreamed changes' '
	(
		cd dst &&
		git pull --rebase &&
		git ls-files -u >untracked &&
		test_must_be_empty untracked
	)
'

test_expect_success 'setup for avoiding reapplying old patches' '
	(
		cd dst &&
		test_might_fail git rebase --abort &&
		git reset --hard origin/main
	) &&
	git clone --bare src src-replace.git &&
	rm -rf src &&
	mv src-replace.git src &&
	(
		cd dst &&
		modify s/2/22/ stuff &&
		git commit -a -m "Change 2" &&
		modify s/3/33/ stuff &&
		git commit -a -m "Change 3" &&
		modify s/4/44/ stuff &&
		git commit -a -m "Change 4" &&
		git push &&

		modify s/44/55/ stuff &&
		git commit --amend -a -m "Modified Change 4"
	)
'

test_expect_success 'git pull --rebase does not reapply old patches' '
	(
		cd dst &&
		test_must_fail git pull --rebase &&
		cat .git/rebase-merge/done .git/rebase-merge/git-rebase-todo >work &&
		grep -v -e \# -e ^$ work >patches &&
		test_line_count = 1 patches &&
		rm -f work
	)
'

test_expect_success 'git pull --rebase against local branch' '
	git checkout -b copy2 to-rebase-orig &&
	git pull --rebase . to-rebase &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2
'

test_done
