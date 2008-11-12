#!/bin/sh

test_description='git repack works correctly'

. ./test-lib.sh

test_expect_success 'objects in packs marked .keep are not repacked' '
	echo content1 > file1 &&
	echo content2 > file2 &&
	git add . &&
	git commit -m initial_commit &&
	# Create two packs
	# The first pack will contain all of the objects except one
	git rev-list --objects --all | grep -v file2 |
		git pack-objects pack > /dev/null &&
	# The second pack will contain the excluded object
	packsha1=$(git rev-list --objects --all | grep file2 |
		git pack-objects pack) &&
	touch -r pack-$packsha1.pack pack-$packsha1.keep &&
	objsha1=$(git verify-pack -v pack-$packsha1.idx | head -n 1 |
		sed -e "s/^\([0-9a-f]\{40\}\).*/\1/") &&
	mv pack-* .git/objects/pack/ &&
	git repack -A -d -l &&
	git prune-packed &&
	for p in .git/objects/pack/*.idx; do
		idx=$(basename $p)
		test "pack-$packsha1.idx" = "$idx" && continue
		if git verify-pack -v $p | egrep "^$objsha1"; then
			found_duplicate_object=1
			echo "DUPLICATE OBJECT FOUND"
			break
		fi
	done &&
	test -z "$found_duplicate_object"
'

test_done

