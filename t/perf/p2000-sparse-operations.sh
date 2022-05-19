#!/bin/sh

test_description="test performance of Git operations using the index"

. ./perf-lib.sh

test_perf_default_repo

SPARSE_CONE=f2/f4

test_expect_success 'setup repo and indexes' '
	but reset --hard HEAD &&

	# Remove submodules from the example repo, because our
	# duplication of the entire repo creates an unlikely data shape.
	if but config --file .butmodules --get-regexp "submodule.*.path" >modules
	then
		but rm $(awk "{print \$2}" modules) &&
		but cummit -m "remove submodules" || return 1
	fi &&

	echo bogus >a &&
	cp a b &&
	but add a b &&
	but cummit -m "level 0" &&
	BLOB=$(but rev-parse HEAD:a) &&
	OLD_CUMMIT=$(but rev-parse HEAD) &&
	OLD_TREE=$(but rev-parse HEAD^{tree}) &&

	for i in $(test_seq 1 3)
	do
		cat >in <<-EOF &&
			100755 blob $BLOB	a
			040000 tree $OLD_TREE	f1
			040000 tree $OLD_TREE	f2
			040000 tree $OLD_TREE	f3
			040000 tree $OLD_TREE	f4
		EOF
		NEW_TREE=$(but mktree <in) &&
		NEW_cummit=$(but cummit-tree $NEW_TREE -p $OLD_CUMMIT -m "level $i") &&
		OLD_TREE=$NEW_TREE &&
		OLD_cummit=$NEW_CUMMIT || return 1
	done &&

	but sparse-checkout init --cone &&
	but sparse-checkout set $SPARSE_CONE &&
	but checkout -b wide $OLD_CUMMIT &&

	for l2 in f1 f2 f3 f4
	do
		echo more bogus >>$SPARSE_CONE/$l2/a &&
		but cummit -a -m "edit $SPARSE_CONE/$l2/a" || return 1
	done &&

	but -c core.sparseCheckoutCone=true clone --branch=wide --sparse . full-v3 &&
	(
		cd full-v3 &&
		but sparse-checkout init --cone &&
		but sparse-checkout set $SPARSE_CONE &&
		but config index.version 3 &&
		but update-index --index-version=3 &&
		but checkout HEAD~4
	) &&
	but -c core.sparseCheckoutCone=true clone --branch=wide --sparse . full-v4 &&
	(
		cd full-v4 &&
		but sparse-checkout init --cone &&
		but sparse-checkout set $SPARSE_CONE &&
		but config index.version 4 &&
		but update-index --index-version=4 &&
		but checkout HEAD~4
	) &&
	but -c core.sparseCheckoutCone=true clone --branch=wide --sparse . sparse-v3 &&
	(
		cd sparse-v3 &&
		but sparse-checkout init --cone --sparse-index &&
		but sparse-checkout set $SPARSE_CONE &&
		but config index.version 3 &&
		but update-index --index-version=3 &&
		but checkout HEAD~4
	) &&
	but -c core.sparseCheckoutCone=true clone --branch=wide --sparse . sparse-v4 &&
	(
		cd sparse-v4 &&
		but sparse-checkout init --cone --sparse-index &&
		but sparse-checkout set $SPARSE_CONE &&
		but config index.version 4 &&
		but update-index --index-version=4 &&
		but checkout HEAD~4
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

test_perf_on_all but status
test_perf_on_all but add -A
test_perf_on_all but add .
test_perf_on_all but cummit -a -m A
test_perf_on_all but checkout -f -
test_perf_on_all but reset
test_perf_on_all but reset --hard
test_perf_on_all but reset -- does-not-exist
test_perf_on_all but diff
test_perf_on_all but diff --cached
test_perf_on_all but blame $SPARSE_CONE/a
test_perf_on_all but blame $SPARSE_CONE/f3/a
test_perf_on_all but read-tree -mu HEAD
test_perf_on_all but checkout-index -f --all
test_perf_on_all but update-index --add --remove $SPARSE_CONE/a

test_done
