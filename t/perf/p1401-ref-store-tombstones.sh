#!/bin/sh

test_description="Tests performance of ref operations with many tombstones"

. ./perf-lib.sh

test_expect_success "setup" '
	git init --ref-format=reftable repo &&
	blob=$(echo foo | git -C repo hash-object -w --stdin) &&
	for i in $(test_seq 8000)
	do
		printf "create refs/tags/tag-%d %s\n" "$i" "$blob" ||
		return 1
	done >repo/input &&
	git -C repo update-ref --stdin <repo/input &&
	git -C repo for-each-ref --format="delete %(refname)" |
	git -C repo update-ref --stdin
'

test_perf "recreate refs after mass delete" '
	git -C repo update-ref --stdin <repo/input &&
	git -C repo for-each-ref --format="delete %(refname)" |
	git -C repo update-ref --stdin
'

test_expect_success "setup asymmetric" '
	git init --ref-format=reftable repo2 &&
	blob=$(echo foo | git -C repo2 hash-object -w --stdin) &&
	for i in $(test_seq 8000)
	do
		printf "create refs/tags/old-%d %s\n" "$i" "$blob" ||
		return 1
	done >repo2/input-old &&
	sed "s/old-/new-/" <repo2/input-old >repo2/input-new &&
	git -C repo2 update-ref --stdin <repo2/input-old &&
	git -C repo2 for-each-ref --format="delete %(refname)" |
	git -C repo2 update-ref --stdin
'

test_perf "create new refs after deleting differently-named refs" '
	git -C repo2 update-ref --stdin <repo2/input-new &&
	git -C repo2 for-each-ref --format="delete %(refname)" refs/tags/ |
	git -C repo2 update-ref --stdin
'

test_done
