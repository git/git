#!/bin/sh

test_description='pulling into void'

. ./test-lib.sh

modify () {
	sed -e "$1" <"$2" >"$2.x" &&
	mv "$2.x" "$2"
}

D=`pwd`

test_expect_success setup '

	echo file >file &&
	git add file &&
	git commit -a -m original

'

test_expect_success 'pulling into void' '
	mkdir cloned &&
	cd cloned &&
	git init &&
	git pull ..
'

cd "$D"

test_expect_success 'checking the results' '
	test -f file &&
	test -f cloned/file &&
	test_cmp file cloned/file
'

test_expect_success 'pulling into void using master:master' '
	mkdir cloned-uho &&
	(
		cd cloned-uho &&
		git init &&
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

test_expect_success 'test . as a remote' '

	git branch copy master &&
	git config branch.copy.remote . &&
	git config branch.copy.merge refs/heads/master &&
	echo updated >file &&
	git commit -a -m updated &&
	git checkout copy &&
	test `cat file` = file &&
	git pull &&
	test `cat file` = updated
'

test_expect_success 'the default remote . should not break explicit pull' '
	git checkout -b second master^ &&
	echo modified >file &&
	git commit -a -m modified &&
	git checkout copy &&
	git reset --hard HEAD^ &&
	test `cat file` = file &&
	git pull . second &&
	test `cat file` = modified
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
	test $(git rev-parse HEAD^) = $(git rev-parse copy) &&
	test new = $(git show HEAD:file2)
'
test_expect_success 'pull.rebase' '
	git reset --hard before-rebase &&
	test_config pull.rebase true &&
	git pull . copy &&
	test $(git rev-parse HEAD^) = $(git rev-parse copy) &&
	test new = $(git show HEAD:file2)
'

test_expect_success 'branch.to-rebase.rebase' '
	git reset --hard before-rebase &&
	test_config branch.to-rebase.rebase true &&
	git pull . copy &&
	test $(git rev-parse HEAD^) = $(git rev-parse copy) &&
	test new = $(git show HEAD:file2)
'

test_expect_success 'branch.to-rebase.rebase should override pull.rebase' '
	git reset --hard before-rebase &&
	test_config pull.rebase true &&
	test_config branch.to-rebase.rebase false &&
	git pull . copy &&
	test $(git rev-parse HEAD^) != $(git rev-parse copy) &&
	test new = $(git show HEAD:file2)
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
	test file = $(cat file2)

'

test_expect_success '--rebase with rebased default upstream' '

	git update-ref refs/remotes/me/copy copy-orig &&
	git checkout --track -b to-rebase2 me/copy &&
	git reset --hard to-rebase-orig &&
	git pull --rebase &&
	test "conflicting modification" = "$(cat file)" &&
	test file = $(cat file2)

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
	COPY=$(git rev-parse --verify me/copy) &&
	git rebase --onto $COPY copy &&
	test_config branch.to-rebase.remote me &&
	test_config branch.to-rebase.merge refs/heads/copy &&
	test_config branch.to-rebase.rebase true &&
	echo dirty >> file &&
	git add file &&
	test_must_fail git pull &&
	test $COPY = $(git rev-parse --verify me/copy) &&
	git checkout HEAD -- file &&
	git pull &&
	test $COPY != $(git rev-parse --verify me/copy)

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

test_expect_success 'git pull that does not say how to integrate' '
	git checkout -b other master^1 &&
	>new &&
	git add new &&
	git commit -m "add new file" &&

	git checkout -b test-to-integrate master &&

	test_config branch.test-to-integrate.remote . &&
	test_config branch.test-to-integrate.merge other &&

	# need real integration
	test_must_fail git pull &&
	git reset --hard master &&


	# configuration is explicit enough
	for how in false true
	do
		test_config pull.rebase $how &&
		git pull &&
		git reset --hard master || break
	done &&

	# per branch configuration is explicit enough
	test_unconfig pull.rebase &&
	for how in false true
	do
		test_config branch.test-to-integrate.rebase $how &&
		git pull &&
		git reset --hard master || break
	done &&

	test_unconfig pull.rebase &&
	test_unconfig branch.test-to-integrate &&

	# already up to date
	git reset --hard master &&
	git branch -f other master^1
	git pull &&

	# fast forward
	git reset --hard master &&
	git checkout -B other master &&
	>new &&
	git add new &&
	git commit -m "add new file" &&
	git checkout -B test-to-integrate master &&
	git pull
'

test_done
