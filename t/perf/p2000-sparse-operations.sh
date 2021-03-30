#!/bin/sh

test_description="test performance of Git operations using the index"

. ./perf-lib.sh

test_perf_default_repo

SPARSE_CONE=f2/f4/f1

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

	for i in $(test_seq 1 4)
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
	git branch -f wide $OLD_COMMIT &&
	git -c core.sparseCheckoutCone=true clone --branch=wide --sparse . full-index-v3 &&
	(
		cd full-index-v3 &&
		git sparse-checkout init --cone &&
		git sparse-checkout set $SPARSE_CONE &&
		git config index.version 3 &&
		git update-index --index-version=3
	) &&
	git -c core.sparseCheckoutCone=true clone --branch=wide --sparse . full-index-v4 &&
	(
		cd full-index-v4 &&
		git sparse-checkout init --cone &&
		git sparse-checkout set $SPARSE_CONE &&
		git config index.version 4 &&
		git update-index --index-version=4
	) &&
	git -c core.sparseCheckoutCone=true clone --branch=wide --sparse . sparse-index-v3 &&
	(
		cd sparse-index-v3 &&
		git sparse-checkout init --cone --sparse-index &&
		git sparse-checkout set $SPARSE_CONE &&
		git config index.version 3 &&
		git update-index --index-version=3
	) &&
	git -c core.sparseCheckoutCone=true clone --branch=wide --sparse . sparse-index-v4 &&
	(
		cd sparse-index-v4 &&
		git sparse-checkout init --cone --sparse-index &&
		git sparse-checkout set $SPARSE_CONE &&
		git config index.version 4 &&
		git update-index --index-version=4
	)
'

test_perf_on_all () {
	command="$@"
	for repo in full-index-v3 full-index-v4 \
		    sparse-index-v3 sparse-index-v4
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
test_perf_on_all git add -A
test_perf_on_all git add .
test_perf_on_all git commit -a -m A

test_done
