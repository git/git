#!/bin/sh

test_description='but repack works correctly'

. ./test-lib.sh
. "${TEST_DIRECTORY}/lib-bitmap.sh"
. "${TEST_DIRECTORY}/lib-midx.sh"
. "${TEST_DIRECTORY}/lib-terminal.sh"

cummit_and_pack () {
	test_cummit "$@" 1>&2 &&
	incrpackid=$(but pack-objects --all --unpacked --incremental .but/objects/pack/pack </dev/null) &&
	echo pack-${incrpackid}.pack
}

test_no_missing_in_packs () {
	myidx=$(ls -1 .but/objects/pack/*.idx) &&
	test_path_is_file "$myidx" &&
	but verify-pack -v alt_objects/pack/*.idx >orig.raw &&
	sed -n -e "s/^\($OID_REGEX\).*/\1/p" orig.raw | sort >orig &&
	but verify-pack -v $myidx >dest.raw &&
	cut -d" " -f1 dest.raw | sort >dest &&
	comm -23 orig dest >missing &&
	test_must_be_empty missing
}

# we expect $packid and $oid to be defined
test_has_duplicate_object () {
	want_duplicate_object="$1"
	found_duplicate_object=false
	for p in .but/objects/pack/*.idx
	do
		idx=$(basename $p)
		test "pack-$packid.idx" = "$idx" && continue
		but verify-pack -v $p >packlist || return $?
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
	but add . &&
	test_tick &&
	but cummit -m initial_cummit &&
	# Create two packs
	# The first pack will contain all of the objects except one
	but rev-list --objects --all >objs &&
	grep -v file2 objs | but pack-objects pack &&
	# The second pack will contain the excluded object
	packid=$(grep file2 objs | but pack-objects pack) &&
	>pack-$packid.keep &&
	but verify-pack -v pack-$packid.idx >packlist &&
	oid=$(head -n 1 packlist | sed -e "s/^\($OID_REGEX\).*/\1/") &&
	mv pack-* .but/objects/pack/ &&
	but repack -A -d -l &&
	but prune-packed &&
	test_has_duplicate_object false
'

test_expect_success 'writing bitmaps via command-line can duplicate .keep objects' '
	# build on $oid, $packid, and .keep state from previous
	BUT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 but repack -Adbl &&
	test_has_duplicate_object true
'

test_expect_success 'writing bitmaps via config can duplicate .keep objects' '
	# build on $oid, $packid, and .keep state from previous
	BUT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
		but -c repack.writebitmaps=true repack -Adl &&
	test_has_duplicate_object true
'

test_expect_success 'loose objects in alternate ODB are not repacked' '
	mkdir alt_objects &&
	echo $(pwd)/alt_objects >.but/objects/info/alternates &&
	echo content3 >file3 &&
	oid=$(BUT_OBJECT_DIRECTORY=alt_objects but hash-object -w file3) &&
	but add file3 &&
	test_tick &&
	but cummit -m cummit_file3 &&
	but repack -a -d -l &&
	but prune-packed &&
	test_has_duplicate_object false
'

test_expect_success 'packed obs in alt ODB are repacked even when local repo is packless' '
	mkdir alt_objects/pack &&
	mv .but/objects/pack/* alt_objects/pack &&
	but repack -a &&
	test_no_missing_in_packs
'

test_expect_success 'packed obs in alt ODB are repacked when local repo has packs' '
	rm -f .but/objects/pack/* &&
	echo new_content >>file1 &&
	but add file1 &&
	test_tick &&
	but cummit -m more_content &&
	but repack &&
	but repack -a -d &&
	test_no_missing_in_packs
'

test_expect_success 'packed obs in alternate ODB kept pack are repacked' '
	# swap the .keep so the cummit object is in the pack with .keep
	for p in alt_objects/pack/*.pack
	do
		base_name=$(basename $p .pack) &&
		if test_path_is_file alt_objects/pack/$base_name.keep
		then
			rm alt_objects/pack/$base_name.keep
		else
			touch alt_objects/pack/$base_name.keep
		fi || return 1
	done &&
	but repack -a -d &&
	test_no_missing_in_packs
'

test_expect_success 'packed unreachable obs in alternate ODB are not loosened' '
	rm -f alt_objects/pack/*.keep &&
	mv .but/objects/pack/* alt_objects/pack/ &&
	coid=$(but rev-parse HEAD^{cummit}) &&
	but reset --hard HEAD^ &&
	test_tick &&
	but reflog expire --expire=$test_tick --expire-unreachable=$test_tick --all &&
	# The pack-objects call on the next line is equivalent to
	# but repack -A -d without the call to prune-packed
	but pack-objects --honor-pack-keep --non-empty --all --reflog \
	    --unpack-unreachable </dev/null pack &&
	rm -f .but/objects/pack/* &&
	mv pack-* .but/objects/pack/ &&
	but verify-pack -v -- .but/objects/pack/*.idx >packlist &&
	! grep "^$coid " packlist &&
	echo >.but/objects/info/alternates &&
	test_must_fail but show $coid
'

test_expect_success 'local packed unreachable obs that exist in alternate ODB are not loosened' '
	echo $(pwd)/alt_objects >.but/objects/info/alternates &&
	echo "$coid" | but pack-objects --non-empty --all --reflog pack &&
	rm -f .but/objects/pack/* &&
	mv pack-* .but/objects/pack/ &&
	# The pack-objects call on the next line is equivalent to
	# but repack -A -d without the call to prune-packed
	but pack-objects --honor-pack-keep --non-empty --all --reflog \
	    --unpack-unreachable </dev/null pack &&
	rm -f .but/objects/pack/* &&
	mv pack-* .but/objects/pack/ &&
	but verify-pack -v -- .but/objects/pack/*.idx >packlist &&
	! grep "^$coid " &&
	echo >.but/objects/info/alternates &&
	test_must_fail but show $coid
'

test_expect_success 'objects made unreachable by grafts only are kept' '
	test_tick &&
	but cummit --allow-empty -m "cummit 4" &&
	H0=$(but rev-parse HEAD) &&
	H1=$(but rev-parse HEAD^) &&
	H2=$(but rev-parse HEAD^^) &&
	echo "$H0 $H2" >.but/info/grafts &&
	but reflog expire --expire=$test_tick --expire-unreachable=$test_tick --all &&
	but repack -a -d &&
	but cat-file -t $H1
'

test_expect_success 'repack --keep-pack' '
	test_create_repo keep-pack &&
	(
		cd keep-pack &&
		P1=$(cummit_and_pack 1) &&
		P2=$(cummit_and_pack 2) &&
		P3=$(cummit_and_pack 3) &&
		P4=$(cummit_and_pack 4) &&
		ls .but/objects/pack/*.pack >old-counts &&
		test_line_count = 4 old-counts &&
		but repack -a -d --keep-pack $P1 --keep-pack $P4 &&
		ls .but/objects/pack/*.pack >new-counts &&
		grep -q $P1 new-counts &&
		grep -q $P4 new-counts &&
		test_line_count = 3 new-counts &&
		but fsck
	)
'

test_expect_success 'bitmaps are created by default in bare repos' '
	but clone --bare .but bare.but &&
	rm -f bare.but/objects/pack/*.bitmap &&
	BUT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
		but -C bare.but repack -ad &&
	bitmap=$(ls bare.but/objects/pack/*.bitmap) &&
	test_path_is_file "$bitmap"
'

test_expect_success 'incremental repack does not complain' '
	but -C bare.but repack -q 2>repack.err &&
	test_must_be_empty repack.err
'

test_expect_success 'bitmaps can be disabled on bare repos' '
	BUT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
		but -c repack.writeBitmaps=false -C bare.but repack -ad &&
	bitmap=$(ls bare.but/objects/pack/*.bitmap || :) &&
	test -z "$bitmap"
'

test_expect_success 'no bitmaps created if .keep files present' '
	pack=$(ls bare.but/objects/pack/*.pack) &&
	test_path_is_file "$pack" &&
	keep=${pack%.pack}.keep &&
	test_when_finished "rm -f \"\$keep\"" &&
	>"$keep" &&
	BUT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
		but -C bare.but repack -ad 2>stderr &&
	test_must_be_empty stderr &&
	find bare.but/objects/pack/ -type f -name "*.bitmap" >actual &&
	test_must_be_empty actual
'

test_expect_success 'auto-bitmaps do not complain if unavailable' '
	test_config -C bare.but pack.packSizeLimit 1M &&
	blob=$(test-tool genrandom big $((1024*1024)) |
	       but -C bare.but hash-object -w --stdin) &&
	but -C bare.but update-ref refs/tags/big $blob &&
	BUT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
		but -C bare.but repack -ad 2>stderr &&
	test_must_be_empty stderr &&
	find bare.but/objects/pack -type f -name "*.bitmap" >actual &&
	test_must_be_empty actual
'

objdir=.but/objects
midx=$objdir/pack/multi-pack-index

test_expect_success 'setup for --write-midx tests' '
	but init midx &&
	(
		cd midx &&
		but config core.multiPackIndex true &&

		test_cummit base
	)
'

test_expect_success '--write-midx unchanged' '
	(
		cd midx &&
		BUT_TEST_MULTI_PACK_INDEX=0 but repack &&
		test_path_is_missing $midx &&
		test_path_is_missing $midx-*.bitmap &&

		BUT_TEST_MULTI_PACK_INDEX=0 but repack --write-midx &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-*.bitmap &&
		test_midx_consistent $objdir
	)
'

test_expect_success '--write-midx with a new pack' '
	(
		cd midx &&
		test_cummit loose &&

		BUT_TEST_MULTI_PACK_INDEX=0 but repack --write-midx &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-*.bitmap &&
		test_midx_consistent $objdir
	)
'

test_expect_success '--write-midx with -b' '
	(
		cd midx &&
		BUT_TEST_MULTI_PACK_INDEX=0 but repack -mb &&

		test_path_is_file $midx &&
		test_path_is_file $midx-*.bitmap &&
		test_midx_consistent $objdir
	)
'

test_expect_success '--write-midx with -d' '
	(
		cd midx &&
		test_cummit repack &&

		BUT_TEST_MULTI_PACK_INDEX=0 but repack -Ad --write-midx &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-*.bitmap &&
		test_midx_consistent $objdir
	)
'

test_expect_success 'cleans up MIDX when appropriate' '
	(
		cd midx &&

		test_cummit repack-2 &&
		BUT_TEST_MULTI_PACK_INDEX=0 but repack -Adb --write-midx &&

		checksum=$(midx_checksum $objdir) &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$checksum.bitmap &&

		test_cummit repack-3 &&
		BUT_TEST_MULTI_PACK_INDEX=0 but repack -Adb --write-midx &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-$checksum.bitmap &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		test_cummit repack-4 &&
		BUT_TEST_MULTI_PACK_INDEX=0 but repack -Adb &&

		find $objdir/pack -type f -name "multi-pack-index*" >files &&
		test_must_be_empty files
	)
'

test_expect_success '--write-midx with preferred bitmap tips' '
	but init midx-preferred-tips &&
	test_when_finished "rm -fr midx-preferred-tips" &&
	(
		cd midx-preferred-tips &&

		test_cummit_bulk --message="%s" 103 &&

		but log --format="%H" >cummits.raw &&
		sort <cummits.raw >cummits &&

		but log --format="create refs/tags/%s/%s %H" HEAD >refs &&
		but update-ref --stdin <refs &&

		but repack --write-midx --write-bitmap-index &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		test-tool bitmap list-cummits | sort >bitmaps &&
		comm -13 bitmaps cummits >before &&
		test_line_count = 1 before &&

		rm -fr $midx-$(midx_checksum $objdir).bitmap &&
		rm -fr $midx &&

		# instead of constructing the snapshot ourselves (c.f., the test
		# "write a bitmap with --refs-snapshot (preferred tips)" in
		# t5326), mark the missing cummit as preferred by adding it to
		# the pack.preferBitmapTips configuration.
		but for-each-ref --format="%(refname:rstrip=1)" \
			--points-at="$(cat before)" >missing &&
		but config pack.preferBitmapTips "$(cat missing)" &&
		but repack --write-midx --write-bitmap-index &&

		test-tool bitmap list-cummits | sort >bitmaps &&
		comm -13 bitmaps cummits >after &&

		! test_cmp before after
	)
'

# The first argument is expected to be a filename
# and that file should contain the name of a .idx
# file. Send the list of objects in that .idx file
# into stdout.
get_sorted_objects_from_pack () {
	but show-index <$(cat "$1") >raw &&
	cut -d" " -f2 raw
}

test_expect_success '--write-midx -b packs non-kept objects' '
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		# Create a kept pack-file
		test_cummit base &&
		but repack -ad &&
		find $objdir/pack -name "*.idx" >before &&
		test_line_count = 1 before &&
		before_name=$(cat before) &&
		>${before_name%.idx}.keep &&

		# Create a non-kept pack-file
		test_cummit other &&
		but repack &&

		# Create loose objects
		test_cummit loose &&

		# Repack everything
		but repack --write-midx -a -b -d &&

		# There should be two pack-files now, the
		# old, kept pack and the new, non-kept pack.
		find $objdir/pack -name "*.idx" | sort >after &&
		test_line_count = 2 after &&
		find $objdir/pack -name "*.keep" >kept &&
		kept_name=$(cat kept) &&
		echo ${kept_name%.keep}.idx >kept-idx &&
		test_cmp before kept-idx &&

		# Get object list from the kept pack.
		get_sorted_objects_from_pack before >old.objects &&

		# Get object list from the one non-kept pack-file
		comm -13 before after >new-pack &&
		test_line_count = 1 new-pack &&
		get_sorted_objects_from_pack new-pack >new.objects &&

		# None of the objects in the new pack should
		# exist within the kept pack.
		comm -12 old.objects new.objects >shared.objects &&
		test_must_be_empty shared.objects
	)
'

test_expect_success TTY '--quiet disables progress' '
	test_terminal env BUT_PROGRESS_DELAY=0 \
		but -C midx repack -ad --quiet --write-midx 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'setup for update-server-info' '
	but init update-server-info &&
	test_cummit -C update-server-info message
'

test_server_info_present () {
	test_path_is_file update-server-info/.but/objects/info/packs &&
	test_path_is_file update-server-info/.but/info/refs
}

test_server_info_missing () {
	test_path_is_missing update-server-info/.but/objects/info/packs &&
	test_path_is_missing update-server-info/.but/info/refs
}

test_server_info_cleanup () {
	rm -f update-server-info/.but/objects/info/packs update-server-info/.but/info/refs &&
	test_server_info_missing
}

test_expect_success 'updates server info by default' '
	test_server_info_cleanup &&
	but -C update-server-info repack &&
	test_server_info_present
'

test_expect_success '-n skips updating server info' '
	test_server_info_cleanup &&
	but -C update-server-info repack -n &&
	test_server_info_missing
'

test_expect_success 'repack.updateServerInfo=true updates server info' '
	test_server_info_cleanup &&
	but -C update-server-info -c repack.updateServerInfo=true repack &&
	test_server_info_present
'

test_expect_success 'repack.updateServerInfo=false skips updating server info' '
	test_server_info_cleanup &&
	but -C update-server-info -c repack.updateServerInfo=false repack &&
	test_server_info_missing
'

test_expect_success '-n overrides repack.updateServerInfo=true' '
	test_server_info_cleanup &&
	but -C update-server-info -c repack.updateServerInfo=true repack -n &&
	test_server_info_missing
'

test_done
