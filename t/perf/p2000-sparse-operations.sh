#!/bin/sh

test_description="test performance of Git operations using the index"

. ./perf-lib.sh

test_perf_default_repo

SPARSE_CONE=f2/f4

test_expect_success 'setup repo and indexes' '
	git reset --hard HEAD &&

	# Remove submodules from the example repo, because our
	# duplication of the entire repo creates an unlikely data shape.
	if git config --file .gitmodules --get-regexp "submodule.*.path" >modules
	then
		git rm $(awk "{print \$2}" modules) &&
		git commit -m "remove submodules" || return 1
	fi &&

	echo bogus >a &&
	cp a b &&
	git add a b &&
	git commit -m "level 0" &&
	BLOB=$(git rev-parse HEAD:a) &&
	OLD_COMMIT=$(git rev-parse HEAD) &&
	OLD_TREE=$(git rev-parse HEAD^{tree}) &&

	for i in $(test_seq 1 3)
	do
		cat >in <<-EOF &&
			100755 blob $BLOB	a
			040000 tree $OLD_TREE	f1
			040000 tree $OLD_TREE	f2
			040000 tree $OLD_TREE	f3
			040000 tree $OLD_TREE	f4
		EOF
		NEW_TREE=$(git mktree <in) &&
		NEW_COMMIT=$(git commit-tree $NEW_TREE -p $OLD_COMMIT -m "level $i") &&
		OLD_TREE=$NEW_TREE &&
		OLD_COMMIT=$NEW_COMMIT || return 1
	done &&

	git sparse-checkout init --cone &&
	git tag -a v1.0 -m "Final" &&
	git sparse-checkout set $SPARSE_CONE &&
	git checkout -b wide $OLD_COMMIT &&

	for l2 in f1 f2 f3 f4
	do
		echo more bogus >>$SPARSE_CONE/$l2/a &&
		git commit -a -m "edit $SPARSE_CONE/$l2/a" || return 1
	done &&

	git -c core.sparseCheckoutCone=true clone --branch=wide --sparse . full-v3 &&
	(
		cd full-v3 &&
		git sparse-checkout init --cone &&
		git sparse-checkout set $SPARSE_CONE &&
		git config index.version 3 &&
		git update-index --index-version=3 &&
		git checkout HEAD~4
	) &&
	git -c core.sparseCheckoutCone=true clone --branch=wide --sparse . full-v4 &&
	(
		cd full-v4 &&
		git sparse-checkout init --cone &&
		git sparse-checkout set $SPARSE_CONE &&
		git config index.version 4 &&
		git update-index --index-version=4 &&
		git checkout HEAD~4
	) &&
	git -c core.sparseCheckoutCone=true clone --branch=wide --sparse . sparse-v3 &&
	(
		cd sparse-v3 &&
		git sparse-checkout init --cone --sparse-index &&
		git sparse-checkout set $SPARSE_CONE &&
		git config index.version 3 &&
		git update-index --index-version=3 &&
		git checkout HEAD~4
	) &&
	git -c core.sparseCheckoutCone=true clone --branch=wide --sparse . sparse-v4 &&
	(
		cd sparse-v4 &&
		git sparse-checkout init --cone --sparse-index &&
		git sparse-checkout set $SPARSE_CONE &&
		git config index.version 4 &&
		git update-index --index-version=4 &&
		git checkout HEAD~4
	)
'

test_perf_on_all () {
	command="$@"
	for repo in full-v3 full-v4 \
		    sparse-v3 sparse-v4
	do
		test_perf "$command ($repo)" "
			(
				cd $repo &&
				echo >>$SPARSE_CONE/a &&
				$command
			)
		"
	done
}

test_perf_on_all git status
test_perf_on_all 'git stash && git stash pop'
test_perf_on_all 'echo >>new && git stash -u && git stash pop'
test_perf_on_all git add -A
test_perf_on_all git add .
test_perf_on_all git commit -a -m A
test_perf_on_all git checkout -f -
test_perf_on_all "git sparse-checkout add f2/f3/f1 && git sparse-checkout set $SPARSE_CONE"
test_perf_on_all git reset
test_perf_on_all git reset --hard
test_perf_on_all git reset -- does-not-exist
test_perf_on_all git diff
test_perf_on_all git diff --cached
test_perf_on_all git blame $SPARSE_CONE/a
test_perf_on_all git blame $SPARSE_CONE/f3/a
test_perf_on_all git read-tree -mu HEAD
test_perf_on_all git checkout-index -f --all
test_perf_on_all git update-index --add --remove $SPARSE_CONE/a
test_perf_on_all "git rm -f $SPARSE_CONE/a && git checkout HEAD -- $SPARSE_CONE/a"
test_perf_on_all git grep --cached bogus -- "f2/f1/f1/*"
test_perf_on_all git write-tree
test_perf_on_all git describe --dirty
test_perf_on_all 'echo >>new && git describe --dirty'
test_perf_on_all git diff-files
test_perf_on_all git diff-files -- $SPARSE_CONE/a
test_perf_on_all git diff-tree HEAD
test_perf_on_all git diff-tree HEAD -- $SPARSE_CONE/a
test_perf_on_all "git worktree add ../temp && git worktree remove ../temp"
test_perf_on_all git check-attr -a -- $SPARSE_CONE/a

test_done
