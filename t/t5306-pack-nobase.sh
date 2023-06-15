#!/bin/sh
#
# Copyright (c) 2008 Google Inc.
#

test_description='git-pack-object with missing base

'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Create A-B chain
#
test_expect_success 'setup base' '
	test_write_lines a b c d e f g h i >text &&
	echo side >side &&
	git update-index --add text side &&
	A=$(echo A | git commit-tree $(git write-tree)) &&

	echo m >>text &&
	git update-index text &&
	B=$(echo B | git commit-tree $(git write-tree) -p $A) &&
	git update-ref HEAD $B
'

# Create repository with C whose parent is B.
# Repository contains C, C^{tree}, C:text, B, B^{tree}.
# Repository is missing B:text (best delta base for C:text).
# Repository is missing A (parent of B).
# Repository is missing A:side.
#
test_expect_success 'setup patch_clone' '
	base_objects=$(pwd)/.git/objects &&
	(mkdir patch_clone &&
	cd patch_clone &&
	git init &&
	echo "$base_objects" >.git/objects/info/alternates &&
	echo q >>text &&
	git read-tree $B &&
	git update-index text &&
	git update-ref HEAD $(echo C | git commit-tree $(git write-tree) -p $B) &&
	rm .git/objects/info/alternates &&

	git --git-dir=../.git cat-file commit $B |
	git hash-object -t commit -w --stdin &&

	git --git-dir=../.git cat-file tree "$B^{tree}" |
	git hash-object -t tree -w --stdin
	) &&
	C=$(git --git-dir=patch_clone/.git rev-parse HEAD)
'

# Clone patch_clone indirectly by cloning base and fetching.
#
test_expect_success 'indirectly clone patch_clone' '
	(mkdir user_clone &&
	 cd user_clone &&
	 git init &&
	 git pull ../.git &&
	 test $(git rev-parse HEAD) = $B &&

	 git pull ../patch_clone/.git &&
	 test $(git rev-parse HEAD) = $C
	)
'

# Cloning the patch_clone directly should fail.
#
test_expect_success 'clone of patch_clone is incomplete' '
	(mkdir user_direct &&
	 cd user_direct &&
	 git init &&
	 test_must_fail git fetch ../patch_clone/.git
	)
'

test_done
