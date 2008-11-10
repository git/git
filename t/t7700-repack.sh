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

test_expect_failure 'loose objects in alternate ODB are not repacked' '
	mkdir alt_objects &&
	echo `pwd`/alt_objects > .git/objects/info/alternates &&
	echo content3 > file3 &&
	objsha1=$(GIT_OBJECT_DIRECTORY=alt_objects git hash-object -w file3) &&
	git add file3 &&
	git commit -m commit_file3 &&
	git repack -a -d -l &&
	git prune-packed &&
	for p in .git/objects/pack/*.idx; do
		if git verify-pack -v $p | egrep "^$objsha1"; then
			found_duplicate_object=1
			echo "DUPLICATE OBJECT FOUND"
			break
		fi
	done &&
	test -z "$found_duplicate_object"
'

test_done

