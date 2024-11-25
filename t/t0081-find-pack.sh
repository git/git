#!/bin/sh

test_description='test `test-tool find-pack`'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit one &&
	test_commit two &&
	test_commit three &&
	test_commit four &&
	test_commit five
'

test_expect_success 'repack everything into a single packfile' '
	git repack -a -d --no-write-bitmap-index &&

	head_commit_pack=$(test-tool find-pack HEAD) &&
	head_tree_pack=$(test-tool find-pack HEAD^{tree}) &&
	one_pack=$(test-tool find-pack HEAD:one.t) &&
	three_pack=$(test-tool find-pack HEAD:three.t) &&
	old_commit_pack=$(test-tool find-pack HEAD~4) &&

	test-tool find-pack --check-count 1 HEAD &&
	test-tool find-pack --check-count=1 HEAD^{tree} &&
	! test-tool find-pack --check-count=0 HEAD:one.t &&
	! test-tool find-pack -c 2 HEAD:one.t &&
	test-tool find-pack -c 1 HEAD:three.t &&

	# Packfile exists at the right path
	case "$head_commit_pack" in
		".git/objects/pack/pack-"*".pack") true ;;
		*) false ;;
	esac &&
	test -f "$head_commit_pack" &&

	# Everything is in the same pack
	test "$head_commit_pack" = "$head_tree_pack" &&
	test "$head_commit_pack" = "$one_pack" &&
	test "$head_commit_pack" = "$three_pack" &&
	test "$head_commit_pack" = "$old_commit_pack"
'

test_expect_success 'add more packfiles' '
	git rev-parse HEAD^{tree} HEAD:two.t HEAD:four.t >objects &&
	git pack-objects .git/objects/pack/mypackname1 >packhash1 <objects &&

	git rev-parse HEAD~ HEAD~^{tree} HEAD:five.t >objects &&
	git pack-objects .git/objects/pack/mypackname2 >packhash2 <objects &&

	head_commit_pack=$(test-tool find-pack HEAD) &&

	# HEAD^{tree} is in 2 packfiles
	test-tool find-pack HEAD^{tree} >head_tree_packs &&
	grep "$head_commit_pack" head_tree_packs &&
	grep mypackname1 head_tree_packs &&
	! grep mypackname2 head_tree_packs &&
	test-tool find-pack --check-count 2 HEAD^{tree} &&
	! test-tool find-pack --check-count 1 HEAD^{tree} &&

	# HEAD:five.t is also in 2 packfiles
	test-tool find-pack HEAD:five.t >five_packs &&
	grep "$head_commit_pack" five_packs &&
	! grep mypackname1 five_packs &&
	grep mypackname2 five_packs &&
	test-tool find-pack -c 2 HEAD:five.t &&
	! test-tool find-pack --check-count=0 HEAD:five.t
'

test_expect_success 'add more commits (as loose objects)' '
	test_commit six &&
	test_commit seven &&

	test -z "$(test-tool find-pack HEAD)" &&
	test -z "$(test-tool find-pack HEAD:six.t)" &&
	test-tool find-pack --check-count 0 HEAD &&
	test-tool find-pack -c 0 HEAD:six.t &&
	! test-tool find-pack -c 1 HEAD:seven.t
'

test_done
