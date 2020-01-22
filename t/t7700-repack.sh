#!/bin/sh

test_description='git repack works correctly'

. ./test-lib.sh

commit_and_pack () {
	test_commit "$@" 1>&2 &&
	incrpackid=$(git pack-objects --all --unpacked --incremental .git/objects/pack/pack </dev/null) &&
	echo pack-${incrpackid}.pack
}

test_no_missing_in_packs () {
	myidx=$(ls -1 .git/objects/pack/*.idx) &&
	test_path_is_file "$myidx" &&
	git verify-pack -v alt_objects/pack/*.idx >orig.raw &&
	sed -n -e "s/^\($OID_REGEX\).*/\1/p" orig.raw | sort >orig &&
	git verify-pack -v $myidx >dest.raw &&
	cut -d" " -f1 dest.raw | sort >dest &&
	comm -23 orig dest >missing &&
	test_must_be_empty missing
}

# we expect $packid and $oid to be defined
test_has_duplicate_object () {
	want_duplicate_object="$1"
	found_duplicate_object=false
	for p in .git/objects/pack/*.idx
	do
		idx=$(basename $p)
		test "pack-$packid.idx" = "$idx" && continue
		git verify-pack -v $p >packlist || return $?
		if grep "^$oid" packlist
		then
			found_duplicate_object=true
			echo "DUPLICATE OBJECT FOUND"
			break
		fi
	done &&
	test "$want_duplicate_object" = "$found_duplicate_object"
}

test_expect_success 'objects in packs marked .keep are not repacked' '
	echo content1 >file1 &&
	echo content2 >file2 &&
	git add . &&
	test_tick &&
	git commit -m initial_commit &&
	# Create two packs
	# The first pack will contain all of the objects except one
	git rev-list --objects --all >objs &&
	grep -v file2 objs | git pack-objects pack &&
	# The second pack will contain the excluded object
	packid=$(grep file2 objs | git pack-objects pack) &&
	>pack-$packid.keep &&
	git verify-pack -v pack-$packid.idx >packlist &&
	oid=$(head -n 1 packlist | sed -e "s/^\($OID_REGEX\).*/\1/") &&
	mv pack-* .git/objects/pack/ &&
	git repack -A -d -l &&
	git prune-packed &&
	test_has_duplicate_object false
'

test_expect_success 'writing bitmaps via command-line can duplicate .keep objects' '
	# build on $oid, $packid, and .keep state from previous
	git repack -Adbl &&
	test_has_duplicate_object true
'

test_expect_success 'writing bitmaps via config can duplicate .keep objects' '
	# build on $oid, $packid, and .keep state from previous
	git -c repack.writebitmaps=true repack -Adl &&
	test_has_duplicate_object true
'

test_expect_success 'loose objects in alternate ODB are not repacked' '
	mkdir alt_objects &&
	echo $(pwd)/alt_objects >.git/objects/info/alternates &&
	echo content3 >file3 &&
	oid=$(GIT_OBJECT_DIRECTORY=alt_objects git hash-object -w file3) &&
	git add file3 &&
	test_tick &&
	git commit -m commit_file3 &&
	git repack -a -d -l &&
	git prune-packed &&
	test_has_duplicate_object false
'

test_expect_success 'packed obs in alt ODB are repacked even when local repo is packless' '
	mkdir alt_objects/pack &&
	mv .git/objects/pack/* alt_objects/pack &&
	git repack -a &&
	test_no_missing_in_packs
'

test_expect_success 'packed obs in alt ODB are repacked when local repo has packs' '
	rm -f .git/objects/pack/* &&
	echo new_content >>file1 &&
	git add file1 &&
	test_tick &&
	git commit -m more_content &&
	git repack &&
	git repack -a -d &&
	test_no_missing_in_packs
'

test_expect_success 'packed obs in alternate ODB kept pack are repacked' '
	# swap the .keep so the commit object is in the pack with .keep
	for p in alt_objects/pack/*.pack
	do
		base_name=$(basename $p .pack) &&
		if test_path_is_file alt_objects/pack/$base_name.keep
		then
			rm alt_objects/pack/$base_name.keep
		else
			touch alt_objects/pack/$base_name.keep
		fi
	done &&
	git repack -a -d &&
	test_no_missing_in_packs
'

test_expect_success 'packed unreachable obs in alternate ODB are not loosened' '
	rm -f alt_objects/pack/*.keep &&
	mv .git/objects/pack/* alt_objects/pack/ &&
	coid=$(git rev-parse HEAD^{commit}) &&
	git reset --hard HEAD^ &&
	test_tick &&
	git reflog expire --expire=$test_tick --expire-unreachable=$test_tick --all &&
	# The pack-objects call on the next line is equivalent to
	# git repack -A -d without the call to prune-packed
	git pack-objects --honor-pack-keep --non-empty --all --reflog \
	    --unpack-unreachable </dev/null pack &&
	rm -f .git/objects/pack/* &&
	mv pack-* .git/objects/pack/ &&
	git verify-pack -v -- .git/objects/pack/*.idx >packlist &&
	! grep "^$coid " packlist &&
	echo >.git/objects/info/alternates &&
	test_must_fail git show $coid
'

test_expect_success 'local packed unreachable obs that exist in alternate ODB are not loosened' '
	echo $(pwd)/alt_objects >.git/objects/info/alternates &&
	echo "$coid" | git pack-objects --non-empty --all --reflog pack &&
	rm -f .git/objects/pack/* &&
	mv pack-* .git/objects/pack/ &&
	# The pack-objects call on the next line is equivalent to
	# git repack -A -d without the call to prune-packed
	git pack-objects --honor-pack-keep --non-empty --all --reflog \
	    --unpack-unreachable </dev/null pack &&
	rm -f .git/objects/pack/* &&
	mv pack-* .git/objects/pack/ &&
	git verify-pack -v -- .git/objects/pack/*.idx >packlist &&
	! grep "^$coid " &&
	echo >.git/objects/info/alternates &&
	test_must_fail git show $coid
'

test_expect_success 'objects made unreachable by grafts only are kept' '
	test_tick &&
	git commit --allow-empty -m "commit 4" &&
	H0=$(git rev-parse HEAD) &&
	H1=$(git rev-parse HEAD^) &&
	H2=$(git rev-parse HEAD^^) &&
	echo "$H0 $H2" >.git/info/grafts &&
	git reflog expire --expire=$test_tick --expire-unreachable=$test_tick --all &&
	git repack -a -d &&
	git cat-file -t $H1
'

test_expect_success 'repack --keep-pack' '
	test_create_repo keep-pack &&
	(
		cd keep-pack &&
		P1=$(commit_and_pack 1) &&
		P2=$(commit_and_pack 2) &&
		P3=$(commit_and_pack 3) &&
		P4=$(commit_and_pack 4) &&
		ls .git/objects/pack/*.pack >old-counts &&
		test_line_count = 4 old-counts &&
		git repack -a -d --keep-pack $P1 --keep-pack $P4 &&
		ls .git/objects/pack/*.pack >new-counts &&
		grep -q $P1 new-counts &&
		grep -q $P4 new-counts &&
		test_line_count = 3 new-counts &&
		git fsck
	)
'

test_expect_success 'bitmaps are created by default in bare repos' '
	git clone --bare .git bare.git &&
	git -C bare.git repack -ad &&
	bitmap=$(ls bare.git/objects/pack/*.bitmap) &&
	test_path_is_file "$bitmap"
'

test_expect_success 'incremental repack does not complain' '
	git -C bare.git repack -q 2>repack.err &&
	test_must_be_empty repack.err
'

test_expect_success 'bitmaps can be disabled on bare repos' '
	git -c repack.writeBitmaps=false -C bare.git repack -ad &&
	bitmap=$(ls bare.git/objects/pack/*.bitmap || :) &&
	test -z "$bitmap"
'

test_expect_success 'no bitmaps created if .keep files present' '
	pack=$(ls bare.git/objects/pack/*.pack) &&
	test_path_is_file "$pack" &&
	keep=${pack%.pack}.keep &&
	test_when_finished "rm -f \"\$keep\"" &&
	>"$keep" &&
	git -C bare.git repack -ad 2>stderr &&
	test_must_be_empty stderr &&
	find bare.git/objects/pack/ -type f -name "*.bitmap" >actual &&
	test_must_be_empty actual
'

test_expect_success 'auto-bitmaps do not complain if unavailable' '
	test_config -C bare.git pack.packSizeLimit 1M &&
	blob=$(test-tool genrandom big $((1024*1024)) |
	       git -C bare.git hash-object -w --stdin) &&
	git -C bare.git update-ref refs/tags/big $blob &&
	git -C bare.git repack -ad 2>stderr &&
	test_must_be_empty stderr &&
	find bare.git/objects/pack -type f -name "*.bitmap" >actual &&
	test_must_be_empty actual
'

test_done
