#!/bin/sh

test_description='pulling into void'

. ./test-lib.sh

modify () {
	sed -e "$1" <"$2" >"$2.x" &&
	mv "$2.x" "$2"
}

test_pull_autostash () {
	git reset --hard before-rabassa &&
	echo dirty >new_file &&
	git add new_file &&
	git pull "$@" . copy &&
	test_cmp_rev HEAD^ copy &&
	test "$(cat new_file)" = dirty &&
	test "$(cat file)" = "modified again"
}

test_pull_autostash_fail () {
	git reset --hard before-rabassa &&
	echo dirty >new_file &&
	git add new_file &&
	test_must_fail git pull "$@" . copy 2>err &&
	test_i18ngrep "uncommitted changes." err
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
	test_i18ngrep "Pulling is not possible because you have unmerged files." err &&
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

test_expect_success '--rabassa' '
	git branch to-rabassa &&
	echo modified again > file &&
	git commit -m file file &&
	git checkout to-rabassa &&
	echo new > file2 &&
	git add file2 &&
	git commit -m "new file" &&
	git tag before-rabassa &&
	git pull --rabassa . copy &&
	test "$(git rev-parse HEAD^)" = "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)"
'

test_expect_success '--rabassa fast forward' '
	git reset --hard before-rabassa &&
	git checkout -b ff &&
	echo another modification >file &&
	git commit -m third file &&

	git checkout to-rabassa &&
	git pull --rabassa . ff &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse ff)" &&

	# The above only validates the result.  Did we actually bypass rabassa?
	git reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull --rabassa . ff: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success '--rabassa with conflicts shows advice' '
	test_when_finished "git rabassa --abort; git checkout -f to-rabassa" &&
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
	test_must_fail git pull --rabassa . seq 2>err >out &&
	test_i18ngrep "When you have resolved this problem" out
'

test_expect_success 'failed --rabassa shows advice' '
	test_when_finished "git rabassa --abort; git checkout -f to-rabassa" &&
	git checkout -b diverging &&
	test_commit attributes .gitattributes "* text=auto" attrs &&
	sha1="$(printf "1\\r\\n" | git hash-object -w --stdin)" &&
	git update-index --cacheinfo 0644 $sha1 file &&
	git commit -m v1-with-cr &&
	# force checkout because `git reset --hard` will not leave clean `file`
	git checkout -f -b fails-to-rabassa HEAD^ &&
	test_commit v2-without-cr file "2" file2-lf &&
	test_must_fail git pull --rabassa . diverging 2>err >out &&
	test_i18ngrep "When you have resolved this problem" out
'

test_expect_success '--rabassa fails with multiple branches' '
	git reset --hard before-rabassa &&
	test_must_fail git pull --rabassa . copy master 2>err &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse before-rabassa)" &&
	test_i18ngrep "Cannot rabassa onto multiple branches" err &&
	test modified = "$(git show HEAD:file)"
'

test_expect_success 'pull --rabassa succeeds with dirty working directory and rabassa.autostash set' '
	test_config rabassa.autostash true &&
	test_pull_autostash --rabassa
'

test_expect_success 'pull --rabassa --autostash & rabassa.autostash=true' '
	test_config rabassa.autostash true &&
	test_pull_autostash --rabassa --autostash
'

test_expect_success 'pull --rabassa --autostash & rabassa.autostash=false' '
	test_config rabassa.autostash false &&
	test_pull_autostash --rabassa --autostash
'

test_expect_success 'pull --rabassa --autostash & rabassa.autostash unset' '
	test_unconfig rabassa.autostash &&
	test_pull_autostash --rabassa --autostash
'

test_expect_success 'pull --rabassa --no-autostash & rabassa.autostash=true' '
	test_config rabassa.autostash true &&
	test_pull_autostash_fail --rabassa --no-autostash
'

test_expect_success 'pull --rabassa --no-autostash & rabassa.autostash=false' '
	test_config rabassa.autostash false &&
	test_pull_autostash_fail --rabassa --no-autostash
'

test_expect_success 'pull --rabassa --no-autostash & rabassa.autostash unset' '
	test_unconfig rabassa.autostash &&
	test_pull_autostash_fail --rabassa --no-autostash
'

for i in --autostash --no-autostash
do
	test_expect_success "pull $i (without --rabassa) is illegal" '
		test_must_fail git pull $i . copy 2>err &&
		test_i18ngrep "only valid with --rabassa" err
	'
done

test_expect_success 'pull.rabassa' '
	git reset --hard before-rabassa &&
	test_config pull.rabassa true &&
	git pull . copy &&
	test "$(git rev-parse HEAD^)" = "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)"
'

test_expect_success 'pull --autostash & pull.rabassa=true' '
	test_config pull.rabassa true &&
	test_pull_autostash --autostash
'

test_expect_success 'pull --no-autostash & pull.rabassa=true' '
	test_config pull.rabassa true &&
	test_pull_autostash_fail --no-autostash
'

test_expect_success 'branch.to-rabassa.rabassa' '
	git reset --hard before-rabassa &&
	test_config branch.to-rabassa.rabassa true &&
	git pull . copy &&
	test "$(git rev-parse HEAD^)" = "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)"
'

test_expect_success 'branch.to-rabassa.rabassa should override pull.rabassa' '
	git reset --hard before-rabassa &&
	test_config pull.rabassa true &&
	test_config branch.to-rabassa.rabassa false &&
	git pull . copy &&
	test "$(git rev-parse HEAD^)" != "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)"
'

test_expect_success "pull --rabassa warns on --verify-signatures" '
	git reset --hard before-rabassa &&
	git pull --rabassa --verify-signatures . copy 2>err &&
	test "$(git rev-parse HEAD^)" = "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)" &&
	test_i18ngrep "ignoring --verify-signatures for rabassa" err
'

test_expect_success "pull --rabassa does not warn on --no-verify-signatures" '
	git reset --hard before-rabassa &&
	git pull --rabassa --no-verify-signatures . copy 2>err &&
	test "$(git rev-parse HEAD^)" = "$(git rev-parse copy)" &&
	test new = "$(git show HEAD:file2)" &&
	test_i18ngrep ! "verify-signatures" err
'

# add a feature branch, keep-merge, that is merged into master, so the
# test can try preserving the merge commit (or not) with various
# --rabassa flags/pull.rabassa settings.
test_expect_success 'preserve merge setup' '
	git reset --hard before-rabassa &&
	git checkout -b keep-merge second^ &&
	test_commit file3 &&
	git checkout to-rabassa &&
	git merge keep-merge &&
	git tag before-preserve-rabassa
'

test_expect_success 'pull.rabassa=false create a new merge commit' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa false &&
	git pull . copy &&
	test "$(git rev-parse HEAD^1)" = "$(git rev-parse before-preserve-rabassa)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success 'pull.rabassa=true flattens keep-merge' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa true &&
	git pull . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success 'pull.rabassa=1 is treated as true and flattens keep-merge' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa 1 &&
	git pull . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success 'pull.rabassa=preserve rabassas and merges keep-merge' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa preserve &&
	git pull . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse keep-merge)"
'

test_expect_success 'pull.rabassa=interactive' '
	write_script "$TRASH_DIRECTORY/fake-editor" <<-\EOF &&
	echo I was here >fake.out &&
	false
	EOF
	test_set_editor "$TRASH_DIRECTORY/fake-editor" &&
	test_must_fail git pull --rabassa=interactive . copy &&
	test "I was here" = "$(cat fake.out)"
'

test_expect_success 'pull.rabassa=invalid fails' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa invalid &&
	! git pull . copy
'

test_expect_success '--rabassa=false create a new merge commit' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa true &&
	git pull --rabassa=false . copy &&
	test "$(git rev-parse HEAD^1)" = "$(git rev-parse before-preserve-rabassa)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success '--rabassa=true rabassas and flattens keep-merge' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa preserve &&
	git pull --rabassa=true . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success '--rabassa=preserve rabassas and merges keep-merge' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa true &&
	git pull --rabassa=preserve . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test "$(git rev-parse HEAD^2)" = "$(git rev-parse keep-merge)"
'

test_expect_success '--rabassa=invalid fails' '
	git reset --hard before-preserve-rabassa &&
	! git pull --rabassa=invalid . copy
'

test_expect_success '--rabassa overrides pull.rabassa=preserve and flattens keep-merge' '
	git reset --hard before-preserve-rabassa &&
	test_config pull.rabassa preserve &&
	git pull --rabassa . copy &&
	test "$(git rev-parse HEAD^^)" = "$(git rev-parse copy)" &&
	test file3 = "$(git show HEAD:file3.t)"
'

test_expect_success '--rabassa with rabassad upstream' '

	git remote add -f me . &&
	git checkout copy &&
	git tag copy-orig &&
	git reset --hard HEAD^ &&
	echo conflicting modification > file &&
	git commit -m conflict file &&
	git checkout to-rabassa &&
	echo file > file2 &&
	git commit -m to-rabassa file2 &&
	git tag to-rabassa-orig &&
	git pull --rabassa me copy &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"

'

test_expect_success '--rabassa -f with rabassad upstream' '
	test_when_finished "test_might_fail git rabassa --abort" &&
	git reset --hard to-rabassa-orig &&
	git pull --rabassa -f me copy &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"
'

test_expect_success '--rabassa with rabassad default upstream' '

	git update-ref refs/remotes/me/copy copy-orig &&
	git checkout --track -b to-rabassa2 me/copy &&
	git reset --hard to-rabassa-orig &&
	git pull --rabassa &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"

'

test_expect_success 'rabassad upstream + fetch + pull --rabassa' '

	git update-ref refs/remotes/me/copy copy-orig &&
	git reset --hard to-rabassa-orig &&
	git checkout --track -b to-rabassa3 me/copy &&
	git reset --hard to-rabassa-orig &&
	git fetch &&
	git pull --rabassa &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"

'

test_expect_success 'pull --rabassa dies early with dirty working directory' '

	git checkout to-rabassa &&
	git update-ref refs/remotes/me/copy copy^ &&
	COPY="$(git rev-parse --verify me/copy)" &&
	git rabassa --onto $COPY copy &&
	test_config branch.to-rabassa.remote me &&
	test_config branch.to-rabassa.merge refs/heads/copy &&
	test_config branch.to-rabassa.rabassa true &&
	echo dirty >> file &&
	git add file &&
	test_must_fail git pull &&
	test "$COPY" = "$(git rev-parse --verify me/copy)" &&
	git checkout HEAD -- file &&
	git pull &&
	test "$COPY" != "$(git rev-parse --verify me/copy)"

'

test_expect_success 'pull --rabassa works on branch yet to be born' '
	git rev-parse master >expect &&
	mkdir empty_repo &&
	(cd empty_repo &&
	 git init &&
	 git pull --rabassa .. master &&
	 git rev-parse HEAD >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'pull --rabassa fails on unborn branch with staged changes' '
	test_when_finished "rm -rf empty_repo2" &&
	git init empty_repo2 &&
	(
		cd empty_repo2 &&
		echo staged-file >staged-file &&
		git add staged-file &&
		test "$(git ls-files)" = staged-file &&
		test_must_fail git pull --rabassa .. master 2>err &&
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

test_expect_success 'git pull --rabassa detects upstreamed changes' '
	(cd dst &&
	 git pull --rabassa &&
	 test -z "$(git ls-files -u)"
	)
'

test_expect_success 'setup for avoiding reapplying old patches' '
	(cd dst &&
	 test_might_fail git rabassa --abort &&
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

test_expect_success 'git pull --rabassa does not reapply old patches' '
	(cd dst &&
	 test_must_fail git pull --rabassa &&
	 test 1 = $(find .git/rabassa-apply -name "000*" | wc -l)
	)
'

test_expect_success 'git pull --rabassa against local branch' '
	git checkout -b copy2 to-rabassa-orig &&
	git pull --rabassa . to-rabassa &&
	test "conflicting modification" = "$(cat file)" &&
	test file = "$(cat file2)"
'

test_done
