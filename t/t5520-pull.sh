#!/bin/sh

test_description='pulling into void'

. ./test-lib.sh

modify () {
	sed -e "$1" <"$2" >"$2.x" &&
	mv "$2.x" "$2"
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
	test -f file &&
	test -f cloned/file &&
	test_cmp file cloned/file
'

test_expect_success 'pulling into void using master:master' '
	git init cloned-uho &&
	(
		cd cloned-uho &&
		git pull .. master:master
	) &&
	test -f file &&
	test -f cloned-uho/file &&
	test_cmp file cloned-uho/file
'

test_expect_success 'pulling into void does not overwrite untracked files' '
	git init cloned-untracked &&
	(
		cd cloned-untracked &&
		echo untracked >file &&
		test_must_fail git pull .. master &&
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
		test_must_fail git pull .. master &&
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
		git pull .. master &&
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
		test_must_fail git pull .. master master &&
		! test -f file
	)
'

test_expect_success 'test . as a remote' '
	git branch copy master &&
	git config branch.copy.remote . &&
	git config branch.copy.merge refs/heads/master &&
	echo updated >file &&
	git commit -a -m updated &&
	git checkout copy &&
	test "$(cat file)" = file &&
	git pull &&
	test "$(cat file)" = updated &&
	git reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success 'the default remote . should not break explicit pull' '
	git checkout -b second master^ &&
	echo modified >file &&
	git commit -a -m modified &&
	git checkout copy &&
	git reset --hard HEAD^ &&
	test "$(cat file)" = file &&
	git pull . second &&
	test "$(cat file)" = modified &&
	git reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull . second: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success 'fail if wildcard spec does not match any refs' '
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	test "$(cat file)" = file &&
	test_must_fail git pull . "refs/nonexisting1/*:refs/nonexisting2/*" 2>err &&
	test_i18ngrep "no candidates for merging" err &&
	test "$(cat file)" = file
'

test_expect_success 'fail if no branches specified with non-default remote' '
	git remote add test_remote . &&
	test_when_finished "git remote remove test_remote" &&
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	test "$(cat file)" = file &&
	test_config branch.test.remote origin &&
	test_must_fail git pull test_remote 2>err &&
	test_i18ngrep "specify a branch on the command line" err &&
	test "$(cat file)" = file
'

test_expect_success 'fail if not on a branch' '
	git remote add origin . &&
	test_when_finished "git remote remove origin" &&
	git checkout HEAD^ &&
	test_when_finished "git checkout -f copy" &&
	test "$(cat file)" = file &&
	test_must_fail git pull 2>err &&
	test_i18ngrep "not currently on a branch" err &&
	test "$(cat file)" = file
'

test_expect_success 'fail if no configuration for current branch' '
	git remote add test_remote . &&
	test_when_finished "git remote remove test_remote" &&
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	test_config branch.test.remote test_remote &&
	test "$(cat file)" = file &&
	test_must_fail git pull 2>err &&
	test_i18ngrep "no tracking information" err &&
	test "$(cat file)" = file
'

test_expect_success 'pull --all: fail if no configuration for current branch' '
	git remote add test_remote . &&
	test_when_finished "git remote remove test_remote" &&
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	test_config branch.test.remote test_remote &&
	test "$(cat file)" = file &&
	test_must_fail git pull --all 2>err &&
	test_i18ngrep "There is no tracking information" err &&
	test "$(cat file)" = file
'

test_expect_success 'fail if upstream branch does not exist' '
	git checkout -b test copy^ &&
	test_when_finished "git checkout -f copy && git branch -D test" &&
	test_config branch.test.remote . &&
	test_config branch.test.merge refs/heads/nonexisting &&
	test "$(cat file)" = file &&
	test_must_fail git pull 2>err &&
	test_i18ngrep "no such ref was fetched" err &&
	test "$(cat file)" = file
'

test_expect_success 'fail if the index has unresolved entries' '
	git checkout -b third second^ &&
	test_when_finished "git checkout -f copy && git branch -D third" &&
	test "$(cat file)" = file &&
	test_commit modified2 file &&
	test -z "$(git ls-files -u)" &&
	test_must_fail git pull . second &&
	test -n "$(git ls-files -u)" &&
	cp file expected &&
	test_must_fail git pull . second 2>err &&
	test_i18ngrep "Pull is not possible because you have unmerged files" err &&
	test_cmp expected file &&
	git add file &&
	test -z "$(git ls-files -u)" &&
	test_must_fail git pull . second 2>err &&
	test_i18ngrep "You have not concluded your merge" err &&
	test_cmp expected file
'

test_expect_success 'fast-forwards working tree if branch head is updated' '
	git checkout -b third second^ &&
	test_when_finished "git checkout -f copy && git branch -D third" &&
	test "$(cat file)" = file &&
	git pull . second:third 2>err &&
	test_i18ngrep "fetch updated the current branch head" err &&
	test "$(cat file)" = modified &&
	test "$(git rev-parse third)" = "$(git rev-parse second)"
'

test_expect_success 'fast-forward fails with conflicting work tree' '
	git checkout -b third second^ &&
	test_when_finished "git checkout -f copy && git branch -D third" &&
	test "$(cat file)" = file &&
	echo conflict >file &&
	test_must_fail git pull . second:third 2>err &&
	test_i18ngrep "Cannot fast-forward your working tree" err &&
	test "$(cat file)" = conflict &&
	test "$(git rev-parse third)" = "$(git rev-parse second)"
'

test_expect_success '--rebase' '
	git branch to-rebase &&
	echo modified again > file &&
	git commit -m file file &&
	git checkout to-rebase &&
	echo new > file2 &&
	git add file2 &&
	git commit -m "new file" &&
	git tag before-rebase &&
	git pull --rebase . copy &&
	test "$(git rev-parse HEAD^)" = "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)"
'

test_expect_success '--rebase fails with multiple branches' '
	git reset --hard before-rebase &&
	test_must_fail git pull --rebase . copy master 2>err &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse before-rebase)" &&
	test_i18ngrep "Cannot rebase onto multiple branches" err &&
	test modified = "$(git show HEAD:file)"
'

test_expect_success 'pull --rebase succeeds with dirty working directory and rebase.autostash set' '
	test_config rebase.autostash true &&
	git reset --hard before-rebase &&
	echo dirty >new_file &&
	git add new_file &&
	git pull --rebase . copy &&
	test_cmp_rev HEAD^ copy &&
	test "$(cat new_file)" = dirty &&
	test "$(cat file)" = "modified again"
'

test_expect_success 'pull.rebase' '
	git reset --hard before-rebase &&
	test_config pull.rebase true &&
	git pull . copy &&
	test "$(git rev-parse HEAD^)" = "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)"
'

test_expect_success 'branch.to-rebase.rebase' '
	git reset --hard before-rebase &&
	test_config branch.to-rebase.rebase true &&
	git pull . copy &&
	test "$(git rev-parse HEAD^)" = "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)"
'

test_expect_success 'branch.to-rebase.rebase should override pull.rebase' '
	git reset --hard before-rebase &&
	test_config pull.rebase true &&
	test_config branch.to-rebase.rebase false &&
	git pull . copy &&
	test "$(git rev-parse HEAD^)" != "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)"
'

# add a feature branch, keep-merge, that is merged into master, so the
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
	test "$(git rev-parse HEAD^1)" = "$(git rev-parse before-preserve-rebase)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success 'pull.rebase=true flattens keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase true &&
	git pull . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success 'pull.rebase=1 is treated as true and flattens keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase 1 &&
	git pull . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success 'pull.rebase=preserve rebases and merges keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase preserve &&
	git pull . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse keep-merge)"
'

test_expect_success 'pull.rebase=invalid fails' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase invalid &&
	! git pull . copy
'

test_expect_success '--rebase=false create a new merge commit' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase true &&
	git pull --rebase=false . copy &&
	test "$(git rev-parse HEAD^1)" = "$(git rev-parse before-preserve-rebase)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success '--rebase=true rebases and flattens keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase preserve &&
	git pull --rebase=true . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success '--rebase=preserve rebases and merges keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase true &&
	git pull --rebase=preserve . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse keep-merge)"
'

test_expect_success '--rebase=invalid fails' '
	git reset --hard before-preserve-rebase &&
	! git pull --rebase=invalid . copy
'

test_expect_success '--rebase overrides pull.rebase=preserve and flattens keep-merge' '
	git reset --hard before-preserve-rebase &&
	test_config pull.rebase preserve &&
	git pull --rebase . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success '--rebase with rebased upstream' '

	git remote add -f me . &&
	git checkout copy &&
	git tag copy-orig &&
	git reset --hard HEAD^ &&
	echo conflicting modification > file &&
	git commit -m conflict file &&
	git checkout to-rebase &&
	echo file > file2 &&
	git commit -m to-rebase file2 &&
	git tag to-rebase-orig &&
	git pull --rebase me copy &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"

'

test_expect_success '--rebase -f with rebased upstream' '
	test_when_finished "test_might_fail git rebase --abort" &&
	git reset --hard to-rebase-orig &&
	git pull --rebase -f me copy &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"
'

test_expect_success '--rebase with rebased default upstream' '

	git update-ref refs/remotes/me/copy copy-orig &&
	git checkout --track -b to-rebase2 me/copy &&
	git reset --hard to-rebase-orig &&
	git pull --rebase &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"

'

test_expect_success 'rebased upstream + fetch + pull --rebase' '

	git update-ref refs/remotes/me/copy copy-orig &&
	git reset --hard to-rebase-orig &&
	git checkout --track -b to-rebase3 me/copy &&
	git reset --hard to-rebase-orig &&
	git fetch &&
	git pull --rebase &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"

'

test_expect_success 'pull --rebase dies early with dirty working directory' '

	git checkout to-rebase &&
	git update-ref refs/remotes/me/copy copy^ &&
	COPY="$(git rev-parse --verify me/copy)" &&
	git rebase --onto $COPY copy &&
	test_config branch.to-rebase.remote me &&
	test_config branch.to-rebase.merge refs/heads/copy &&
	test_config branch.to-rebase.rebase true &&
	echo dirty >> file &&
	git add file &&
	test_must_fail git pull &&
	test "$COPY" = "$(git rev-parse --verify me/copy)" &&
	git checkout HEAD -- file &&
	git pull &&
	test "$COPY" != "$(git rev-parse --verify me/copy)"

'

test_expect_success 'pull --rebase works on branch yet to be born' '
	git rev-parse master >expect &&
	mkdir empty_repo &&
	(cd empty_repo &&
	 git init &&
	 git pull --rebase .. master &&
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
		test "$(git ls-files)" = staged-file &&
		test_must_fail git pull --rebase .. master 2>err &&
		test "$(git ls-files)" = staged-file &&
		test "$(git show :staged-file)" = staged-file &&
		test_i18ngrep "unborn branch with changes added to the index" err
	)
'

test_expect_success 'setup for detecting upstreamed changes' '
	mkdir src &&
	(cd src &&
	 git init &&
	 printf "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" > stuff &&
	 git add stuff &&
	 git commit -m "Initial revision"
	) &&
	git clone src dst &&
	(cd src &&
	 modify s/5/43/ stuff &&
	 git commit -a -m "5->43" &&
	 modify s/6/42/ stuff &&
	 git commit -a -m "Make it bigger"
	) &&
	(cd dst &&
	 modify s/5/43/ stuff &&
	 git commit -a -m "Independent discovery of 5->43"
	)
'

test_expect_success 'git pull --rebase detects upstreamed changes' '
	(cd dst &&
	 git pull --rebase &&
	 test -z "$(git ls-files -u)"
	)
'

test_expect_success 'setup for avoiding reapplying old patches' '
	(cd dst &&
	 test_might_fail git rebase --abort &&
	 git reset --hard origin/master
	) &&
	git clone --bare src src-replace.git &&
	rm -rf src &&
	mv src-replace.git src &&
	(cd dst &&
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
	(cd dst &&
	 test_must_fail git pull --rebase &&
	 test 1 = $(find .git/rebase-apply -name "000*" | wc -l)
	)
'

test_expect_success 'git pull --rebase against local branch' '
	git checkout -b copy2 to-rebase-orig &&
	git pull --rebase . to-rebase &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"
'

test_done
