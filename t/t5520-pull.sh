#!/bin/sh

test_description='pulling into void'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

modify () {
	sed -e "$1" "$2" >"$2.x" &&
	mv "$2.x" "$2"
}

test_pull_autostash () {
	expect_parent_num="$1" &&
	shift &&
	but reset --hard before-rebase &&
	echo dirty >new_file &&
	but add new_file &&
	but pull "$@" . copy &&
	test_cmp_rev HEAD^"$expect_parent_num" copy &&
	echo dirty >expect &&
	test_cmp expect new_file &&
	echo "modified again" >expect &&
	test_cmp expect file
}

test_pull_autostash_fail () {
	but reset --hard before-rebase &&
	echo dirty >new_file &&
	but add new_file &&
	test_must_fail but pull "$@" . copy 2>err &&
	test_i18ngrep -E "uncummitted changes.|overwritten by merge:" err
}

test_expect_success setup '
	echo file >file &&
	but add file &&
	but cummit -a -m original
'

test_expect_success 'pulling into void' '
	but init cloned &&
	(
		cd cloned &&
		but pull ..
	) &&
	test_path_is_file file &&
	test_path_is_file cloned/file &&
	test_cmp file cloned/file
'

test_expect_success 'pulling into void using main:main' '
	but init cloned-uho &&
	(
		cd cloned-uho &&
		but pull .. main:main
	) &&
	test_path_is_file file &&
	test_path_is_file cloned-uho/file &&
	test_cmp file cloned-uho/file
'

test_expect_success 'pulling into void does not overwrite untracked files' '
	but init cloned-untracked &&
	(
		cd cloned-untracked &&
		echo untracked >file &&
		test_must_fail but pull .. main &&
		echo untracked >expect &&
		test_cmp expect file
	)
'

test_expect_success 'pulling into void does not overwrite staged files' '
	but init cloned-staged-colliding &&
	(
		cd cloned-staged-colliding &&
		echo "alternate content" >file &&
		but add file &&
		test_must_fail but pull .. main &&
		echo "alternate content" >expect &&
		test_cmp expect file &&
		but cat-file blob :file >file.index &&
		test_cmp expect file.index
	)
'

test_expect_success 'pulling into void does not remove new staged files' '
	but init cloned-staged-new &&
	(
		cd cloned-staged-new &&
		echo "new tracked file" >newfile &&
		but add newfile &&
		but pull .. main &&
		echo "new tracked file" >expect &&
		test_cmp expect newfile &&
		but cat-file blob :newfile >newfile.index &&
		test_cmp expect newfile.index
	)
'

test_expect_success 'pulling into void must not create an octopus' '
	but init cloned-octopus &&
	(
		cd cloned-octopus &&
		test_must_fail but pull .. main main &&
		test_path_is_missing file
	)
'

test_expect_success 'test . as a remote' '
	but branch copy main &&
	but config branch.copy.remote . &&
	but config branch.copy.merge refs/heads/main &&
	echo updated >file &&
	but cummit -a -m updated &&
	but checkout copy &&
	echo file >expect &&
	test_cmp expect file &&
	but pull &&
	echo updated >expect &&
	test_cmp expect file &&
	but reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success 'the default remote . should not break explicit pull' '
	but checkout -b second main^ &&
	echo modified >file &&
	but cummit -a -m modified &&
	but checkout copy &&
	but reset --hard HEAD^ &&
	echo file >expect &&
	test_cmp expect file &&
	but pull --no-rebase . second &&
	echo modified >expect &&
	test_cmp expect file &&
	but reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull --no-rebase . second: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success 'fail if wildcard spec does not match any refs' '
	but checkout -b test copy^ &&
	test_when_finished "but checkout -f copy && but branch -D test" &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail but pull . "refs/nonexisting1/*:refs/nonexisting2/*" 2>err &&
	test_i18ngrep "no candidates for merging" err &&
	test_cmp expect file
'

test_expect_success 'fail if no branches specified with non-default remote' '
	but remote add test_remote . &&
	test_when_finished "but remote remove test_remote" &&
	but checkout -b test copy^ &&
	test_when_finished "but checkout -f copy && but branch -D test" &&
	echo file >expect &&
	test_cmp expect file &&
	test_config branch.test.remote origin &&
	test_must_fail but pull test_remote 2>err &&
	test_i18ngrep "specify a branch on the command line" err &&
	test_cmp expect file
'

test_expect_success 'fail if not on a branch' '
	but remote add origin . &&
	test_when_finished "but remote remove origin" &&
	but checkout HEAD^ &&
	test_when_finished "but checkout -f copy" &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail but pull 2>err &&
	test_i18ngrep "not currently on a branch" err &&
	test_cmp expect file
'

test_expect_success 'fail if no configuration for current branch' '
	but remote add test_remote . &&
	test_when_finished "but remote remove test_remote" &&
	but checkout -b test copy^ &&
	test_when_finished "but checkout -f copy && but branch -D test" &&
	test_config branch.test.remote test_remote &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail but pull 2>err &&
	test_i18ngrep "no tracking information" err &&
	test_cmp expect file
'

test_expect_success 'pull --all: fail if no configuration for current branch' '
	but remote add test_remote . &&
	test_when_finished "but remote remove test_remote" &&
	but checkout -b test copy^ &&
	test_when_finished "but checkout -f copy && but branch -D test" &&
	test_config branch.test.remote test_remote &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail but pull --all 2>err &&
	test_i18ngrep "There is no tracking information" err &&
	test_cmp expect file
'

test_expect_success 'fail if upstream branch does not exist' '
	but checkout -b test copy^ &&
	test_when_finished "but checkout -f copy && but branch -D test" &&
	test_config branch.test.remote . &&
	test_config branch.test.merge refs/heads/nonexisting &&
	echo file >expect &&
	test_cmp expect file &&
	test_must_fail but pull 2>err &&
	test_i18ngrep "no such ref was fetched" err &&
	test_cmp expect file
'

test_expect_success 'fail if the index has unresolved entries' '
	but checkout -b third second^ &&
	test_when_finished "but checkout -f copy && but branch -D third" &&
	echo file >expect &&
	test_cmp expect file &&
	test_cummit modified2 file &&
	but ls-files -u >unmerged &&
	test_must_be_empty unmerged &&
	test_must_fail but pull --no-rebase . second &&
	but ls-files -u >unmerged &&
	test_file_not_empty unmerged &&
	cp file expected &&
	test_must_fail but pull . second 2>err &&
	test_i18ngrep "Pulling is not possible because you have unmerged files." err &&
	test_cmp expected file &&
	but add file &&
	but ls-files -u >unmerged &&
	test_must_be_empty unmerged &&
	test_must_fail but pull . second 2>err &&
	test_i18ngrep "You have not concluded your merge" err &&
	test_cmp expected file
'

test_expect_success 'fast-forwards working tree if branch head is updated' '
	but checkout -b third second^ &&
	test_when_finished "but checkout -f copy && but branch -D third" &&
	echo file >expect &&
	test_cmp expect file &&
	but pull . second:third 2>err &&
	test_i18ngrep "fetch updated the current branch head" err &&
	echo modified >expect &&
	test_cmp expect file &&
	test_cmp_rev third second
'

test_expect_success 'fast-forward fails with conflicting work tree' '
	but checkout -b third second^ &&
	test_when_finished "but checkout -f copy && but branch -D third" &&
	echo file >expect &&
	test_cmp expect file &&
	echo conflict >file &&
	test_must_fail but pull . second:third 2>err &&
	test_i18ngrep "Cannot fast-forward your working tree" err &&
	echo conflict >expect &&
	test_cmp expect file &&
	test_cmp_rev third second
'

test_expect_success '--rebase' '
	but branch to-rebase &&
	echo modified again >file &&
	but cummit -m file file &&
	but checkout to-rebase &&
	echo new >file2 &&
	but add file2 &&
	but cummit -m "new file" &&
	but tag before-rebase &&
	but pull --rebase . copy &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	but show HEAD:file2 >actual &&
	test_cmp expect actual
'

test_expect_success '--rebase (merge) fast forward' '
	but reset --hard before-rebase &&
	but checkout -b ff &&
	echo another modification >file &&
	but cummit -m third file &&

	but checkout to-rebase &&
	but -c rebase.backend=merge pull --rebase . ff &&
	test_cmp_rev HEAD ff &&

	# The above only validates the result.  Did we actually bypass rebase?
	but reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull --rebase . ff: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success '--rebase (am) fast forward' '
	but reset --hard before-rebase &&

	but -c rebase.backend=apply pull --rebase . ff &&
	test_cmp_rev HEAD ff &&

	# The above only validates the result.  Did we actually bypass rebase?
	but reflog -1 >reflog.actual &&
	sed "s/^[0-9a-f][0-9a-f]*/OBJID/" reflog.actual >reflog.fuzzy &&
	echo "OBJID HEAD@{0}: pull --rebase . ff: Fast-forward" >reflog.expected &&
	test_cmp reflog.expected reflog.fuzzy
'

test_expect_success '--rebase --autostash fast forward' '
	test_when_finished "
		but reset --hard
		but checkout to-rebase
		but branch -D to-rebase-ff
		but branch -D behind" &&
	but branch behind &&
	but checkout -b to-rebase-ff &&
	echo another modification >>file &&
	but add file &&
	but cummit -m mod &&

	but checkout behind &&
	echo dirty >file &&
	but pull --rebase --autostash . to-rebase-ff &&
	test_cmp_rev HEAD to-rebase-ff
'

test_expect_success '--rebase with rebase.autostash succeeds on ff' '
	test_when_finished "rm -fr src dst actual" &&
	but init src &&
	test_cummit -C src "initial" file "content" &&
	but clone src dst &&
	test_cummit -C src --printf "more_content" file "more content\ncontent\n" &&
	echo "dirty" >>dst/file &&
	test_config -C dst rebase.autostash true &&
	but -C dst pull --rebase >actual 2>&1 &&
	grep -q "Fast-forward" actual &&
	grep -q "Applied autostash." actual
'

test_expect_success '--rebase with conflicts shows advice' '
	test_when_finished "but rebase --abort; but checkout -f to-rebase" &&
	but checkout -b seq &&
	test_seq 5 >seq.txt &&
	but add seq.txt &&
	test_tick &&
	but cummit -m "Add seq.txt" &&
	echo 6 >>seq.txt &&
	test_tick &&
	but cummit -m "Append to seq.txt" seq.txt &&
	but checkout -b with-conflicts HEAD^ &&
	echo conflicting >>seq.txt &&
	test_tick &&
	but cummit -m "Create conflict" seq.txt &&
	test_must_fail but pull --rebase . seq 2>err >out &&
	test_i18ngrep "Resolve all conflicts manually" err
'

test_expect_success 'failed --rebase shows advice' '
	test_when_finished "but rebase --abort; but checkout -f to-rebase" &&
	but checkout -b diverging &&
	test_cummit attributes .butattributes "* text=auto" attrs &&
	sha1="$(printf "1\\r\\n" | but hash-object -w --stdin)" &&
	but update-index --cacheinfo 0644 $sha1 file &&
	but cummit -m v1-with-cr &&
	# force checkout because `but reset --hard` will not leave clean `file`
	but checkout -f -b fails-to-rebase HEAD^ &&
	test_cummit v2-without-cr file "2" file2-lf &&
	test_must_fail but pull --rebase . diverging 2>err >out &&
	test_i18ngrep "Resolve all conflicts manually" err
'

test_expect_success '--rebase fails with multiple branches' '
	but reset --hard before-rebase &&
	test_must_fail but pull --rebase . copy main 2>err &&
	test_cmp_rev HEAD before-rebase &&
	test_i18ngrep "Cannot rebase onto multiple branches" err &&
	echo modified >expect &&
	but show HEAD:file >actual &&
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
	but reset --hard before-rebase &&
	test_config pull.rebase true &&
	but pull . copy &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	but show HEAD:file2 >actual &&
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
	but reset --hard before-rebase &&
	test_config branch.to-rebase.rebase true &&
	but pull . copy &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	but show HEAD:file2 >actual &&
	test_cmp expect actual
'

test_expect_success 'branch.to-rebase.rebase should override pull.rebase' '
	but reset --hard before-rebase &&
	test_config pull.rebase true &&
	test_config branch.to-rebase.rebase false &&
	but pull . copy &&
	test_cmp_rev ! HEAD^ copy &&
	echo new >expect &&
	but show HEAD:file2 >actual &&
	test_cmp expect actual
'

test_expect_success 'pull --rebase warns on --verify-signatures' '
	but reset --hard before-rebase &&
	but pull --rebase --verify-signatures . copy 2>err &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	but show HEAD:file2 >actual &&
	test_cmp expect actual &&
	test_i18ngrep "ignoring --verify-signatures for rebase" err
'

test_expect_success 'pull --rebase does not warn on --no-verify-signatures' '
	but reset --hard before-rebase &&
	but pull --rebase --no-verify-signatures . copy 2>err &&
	test_cmp_rev HEAD^ copy &&
	echo new >expect &&
	but show HEAD:file2 >actual &&
	test_cmp expect actual &&
	test_i18ngrep ! "verify-signatures" err
'

# add a feature branch, keep-merge, that is merged into main, so the
# test can try preserving the merge cummit (or not) with various
# --rebase flags/pull.rebase settings.
test_expect_success 'preserve merge setup' '
	but reset --hard before-rebase &&
	but checkout -b keep-merge second^ &&
	test_cummit file3 &&
	but checkout to-rebase &&
	but merge keep-merge &&
	but tag before-preserve-rebase
'

test_expect_success 'pull.rebase=false create a new merge cummit' '
	but reset --hard before-preserve-rebase &&
	test_config pull.rebase false &&
	but pull . copy &&
	test_cmp_rev HEAD^1 before-preserve-rebase &&
	test_cmp_rev HEAD^2 copy &&
	echo file3 >expect &&
	but show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success 'pull.rebase=true flattens keep-merge' '
	but reset --hard before-preserve-rebase &&
	test_config pull.rebase true &&
	but pull . copy &&
	test_cmp_rev HEAD^^ copy &&
	echo file3 >expect &&
	but show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success 'pull.rebase=1 is treated as true and flattens keep-merge' '
	but reset --hard before-preserve-rebase &&
	test_config pull.rebase 1 &&
	but pull . copy &&
	test_cmp_rev HEAD^^ copy &&
	echo file3 >expect &&
	but show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success 'pull.rebase=interactive' '
	write_script "$TRASH_DIRECTORY/fake-editor" <<-\EOF &&
	echo I was here >fake.out &&
	false
	EOF
	test_set_editor "$TRASH_DIRECTORY/fake-editor" &&
	test_when_finished "test_might_fail but rebase --abort" &&
	test_must_fail but pull --rebase=interactive . copy &&
	echo "I was here" >expect &&
	test_cmp expect fake.out
'

test_expect_success 'pull --rebase=i' '
	write_script "$TRASH_DIRECTORY/fake-editor" <<-\EOF &&
	echo I was here, too >fake.out &&
	false
	EOF
	test_set_editor "$TRASH_DIRECTORY/fake-editor" &&
	test_when_finished "test_might_fail but rebase --abort" &&
	test_must_fail but pull --rebase=i . copy &&
	echo "I was here, too" >expect &&
	test_cmp expect fake.out
'

test_expect_success 'pull.rebase=invalid fails' '
	but reset --hard before-preserve-rebase &&
	test_config pull.rebase invalid &&
	test_must_fail but pull . copy
'

test_expect_success '--rebase=false create a new merge cummit' '
	but reset --hard before-preserve-rebase &&
	test_config pull.rebase true &&
	but pull --rebase=false . copy &&
	test_cmp_rev HEAD^1 before-preserve-rebase &&
	test_cmp_rev HEAD^2 copy &&
	echo file3 >expect &&
	but show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success '--rebase=true rebases and flattens keep-merge' '
	but reset --hard before-preserve-rebase &&
	test_config pull.rebase merges &&
	but pull --rebase=true . copy &&
	test_cmp_rev HEAD^^ copy &&
	echo file3 >expect &&
	but show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success '--rebase=invalid fails' '
	but reset --hard before-preserve-rebase &&
	test_must_fail but pull --rebase=invalid . copy
'

test_expect_success '--rebase overrides pull.rebase=merges and flattens keep-merge' '
	but reset --hard before-preserve-rebase &&
	test_config pull.rebase merges &&
	but pull --rebase . copy &&
	test_cmp_rev HEAD^^ copy &&
	echo file3 >expect &&
	but show HEAD:file3.t >actual &&
	test_cmp expect actual
'

test_expect_success '--rebase with rebased upstream' '
	but remote add -f me . &&
	but checkout copy &&
	but tag copy-orig &&
	but reset --hard HEAD^ &&
	echo conflicting modification >file &&
	but cummit -m conflict file &&
	but checkout to-rebase &&
	echo file >file2 &&
	but cummit -m to-rebase file2 &&
	but tag to-rebase-orig &&
	but pull --rebase me copy &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2
'

test_expect_success '--rebase -f with rebased upstream' '
	test_when_finished "test_might_fail but rebase --abort" &&
	but reset --hard to-rebase-orig &&
	but pull --rebase -f me copy &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2
'

test_expect_success '--rebase with rebased default upstream' '
	but update-ref refs/remotes/me/copy copy-orig &&
	but checkout --track -b to-rebase2 me/copy &&
	but reset --hard to-rebase-orig &&
	but pull --rebase &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2
'

test_expect_success 'rebased upstream + fetch + pull --rebase' '

	but update-ref refs/remotes/me/copy copy-orig &&
	but reset --hard to-rebase-orig &&
	but checkout --track -b to-rebase3 me/copy &&
	but reset --hard to-rebase-orig &&
	but fetch &&
	but pull --rebase &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2

'

test_expect_success 'pull --rebase dies early with dirty working directory' '
	but checkout to-rebase &&
	but update-ref refs/remotes/me/copy copy^ &&
	COPY="$(but rev-parse --verify me/copy)" &&
	but rebase --onto $COPY copy &&
	test_config branch.to-rebase.remote me &&
	test_config branch.to-rebase.merge refs/heads/copy &&
	test_config branch.to-rebase.rebase true &&
	echo dirty >>file &&
	but add file &&
	test_must_fail but pull &&
	test_cmp_rev "$COPY" me/copy &&
	but checkout HEAD -- file &&
	but pull &&
	test_cmp_rev ! "$COPY" me/copy
'

test_expect_success 'pull --rebase works on branch yet to be born' '
	but rev-parse main >expect &&
	mkdir empty_repo &&
	(
		cd empty_repo &&
		but init &&
		but pull --rebase .. main &&
		but rev-parse HEAD >../actual
	) &&
	test_cmp expect actual
'

test_expect_success 'pull --rebase fails on unborn branch with staged changes' '
	test_when_finished "rm -rf empty_repo2" &&
	but init empty_repo2 &&
	(
		cd empty_repo2 &&
		echo staged-file >staged-file &&
		but add staged-file &&
		echo staged-file >expect &&
		but ls-files >actual &&
		test_cmp expect actual &&
		test_must_fail but pull --rebase .. main 2>err &&
		but ls-files >actual &&
		test_cmp expect actual &&
		but show :staged-file >actual &&
		test_cmp expect actual &&
		test_i18ngrep "unborn branch with changes added to the index" err
	)
'

test_expect_success 'pull --rebase fails on corrupt HEAD' '
	test_when_finished "rm -rf corrupt" &&
	but init corrupt &&
	(
		cd corrupt &&
		test_cummit one &&
		but rev-parse --verify HEAD >head &&
		obj=$(sed "s#^..#&/#" head) &&
		rm -f .but/objects/$obj &&
		test_must_fail but pull --rebase
	)
'

test_expect_success 'setup for detecting upstreamed changes' '
	test_create_repo src &&
	test_cummit -C src --printf one stuff "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n" &&
	but clone src dst &&
	(
		cd src &&
		modify s/5/43/ stuff &&
		but cummit -a -m "5->43" &&
		modify s/6/42/ stuff &&
		but cummit -a -m "Make it bigger"
	) &&
	(
		cd dst &&
		modify s/5/43/ stuff &&
		but cummit -a -m "Independent discovery of 5->43"
	)
'

test_expect_success 'but pull --rebase detects upstreamed changes' '
	(
		cd dst &&
		but pull --rebase &&
		but ls-files -u >untracked &&
		test_must_be_empty untracked
	)
'

test_expect_success 'setup for avoiding reapplying old patches' '
	(
		cd dst &&
		test_might_fail but rebase --abort &&
		but reset --hard origin/main
	) &&
	but clone --bare src src-replace.but &&
	rm -rf src &&
	mv src-replace.but src &&
	(
		cd dst &&
		modify s/2/22/ stuff &&
		but cummit -a -m "Change 2" &&
		modify s/3/33/ stuff &&
		but cummit -a -m "Change 3" &&
		modify s/4/44/ stuff &&
		but cummit -a -m "Change 4" &&
		but push &&

		modify s/44/55/ stuff &&
		but cummit --amend -a -m "Modified Change 4"
	)
'

test_expect_success 'but pull --rebase does not reapply old patches' '
	(
		cd dst &&
		test_must_fail but pull --rebase &&
		cat .but/rebase-merge/done .but/rebase-merge/but-rebase-todo >work &&
		grep -v -e \# -e ^$ work >patches &&
		test_line_count = 1 patches &&
		rm -f work
	)
'

test_expect_success 'but pull --rebase against local branch' '
	but checkout -b copy2 to-rebase-orig &&
	but pull --rebase . to-rebase &&
	echo "conflicting modification" >expect &&
	test_cmp expect file &&
	echo file >expect &&
	test_cmp expect file2
'

test_done
