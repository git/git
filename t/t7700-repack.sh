#!/bin/sh

test_description='git repack works correctly'

. ./test-lib.sh
. "${TEST_DIRECTORY}/lib-bitmap.sh"
. "${TEST_DIRECTORY}/lib-midx.sh"
. "${TEST_DIRECTORY}/lib-terminal.sh"

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
	GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 git repack -Adbl &&
	test_has_duplicate_object true
'

test_expect_success 'writing bitmaps via config can duplicate .keep objects' '
	# build on $oid, $packid, and .keep state from previous
	GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
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

test_expect_success SYMLINKS '--local keeps packs when alternate is objectdir ' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo A &&
	(
		cd repo &&
		git repack -a &&
		ls .git/objects/pack/*.pack >../expect &&
		ln -s objects .git/alt_objects &&
		echo "$(pwd)/.git/alt_objects" >.git/objects/info/alternates &&
		git repack -a -d -l &&
		ls .git/objects/pack/*.pack >../actual
	) &&
	test_cmp expect actual
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
		fi || return 1
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
	rm -f bare.git/objects/pack/*.bitmap &&
	GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
		git -C bare.git repack -ad &&
	bitmap=$(ls bare.git/objects/pack/*.bitmap) &&
	test_path_is_file "$bitmap"
'

test_expect_success 'incremental repack does not complain' '
	git -C bare.git repack -q 2>repack.err &&
	test_must_be_empty repack.err
'

test_expect_success 'bitmaps can be disabled on bare repos' '
	GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
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
	GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
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
	GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
		git -C bare.git repack -ad 2>stderr &&
	test_must_be_empty stderr &&
	find bare.git/objects/pack -type f -name "*.bitmap" >actual &&
	test_must_be_empty actual
'

test_expect_success 'repacking with a filter works' '
	test_when_finished "rm -rf server client" &&
	test_create_repo server &&
	git -C server config uploadpack.allowFilter true &&
	git -C server config uploadpack.allowAnySHA1InWant true &&
	test_commit -C server 1 &&
	git clone --bare --no-local server client &&
	git -C client config remote.origin.promisor true &&
	git -C client rev-list --objects --all --missing=print >objects &&
	test $(grep "^?" objects | wc -l) = 0 &&
	git -C client -c repack.writebitmaps=false repack -a -d --filter=blob:none &&
	git -C client rev-list --objects --all --missing=print >objects &&
	test $(grep "^?" objects | wc -l) = 1
'

objdir=.git/objects
midx=$objdir/pack/multi-pack-index

test_expect_success 'setup for --write-midx tests' '
	git init midx &&
	(
		cd midx &&
		git config core.multiPackIndex true &&

		test_commit base
	)
'

test_expect_success '--write-midx unchanged' '
	(
		cd midx &&
		GIT_TEST_MULTI_PACK_INDEX=0 git repack &&
		test_path_is_missing $midx &&
		test_path_is_missing $midx-*.bitmap &&

		GIT_TEST_MULTI_PACK_INDEX=0 git repack --write-midx &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-*.bitmap &&
		test_midx_consistent $objdir
	)
'

test_expect_success '--write-midx with a new pack' '
	(
		cd midx &&
		test_commit loose &&

		GIT_TEST_MULTI_PACK_INDEX=0 git repack --write-midx &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-*.bitmap &&
		test_midx_consistent $objdir
	)
'

test_expect_success '--write-midx with -b' '
	(
		cd midx &&
		GIT_TEST_MULTI_PACK_INDEX=0 git repack -mb &&

		test_path_is_file $midx &&
		test_path_is_file $midx-*.bitmap &&
		test_midx_consistent $objdir
	)
'

test_expect_success '--write-midx with -d' '
	(
		cd midx &&
		test_commit repack &&

		GIT_TEST_MULTI_PACK_INDEX=0 git repack -Ad --write-midx &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-*.bitmap &&
		test_midx_consistent $objdir
	)
'

test_expect_success 'cleans up MIDX when appropriate' '
	(
		cd midx &&

		test_commit repack-2 &&
		GIT_TEST_MULTI_PACK_INDEX=0 git repack -Adb --write-midx &&

		checksum=$(midx_checksum $objdir) &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$checksum.bitmap &&

		test_commit repack-3 &&
		GIT_TEST_MULTI_PACK_INDEX=0 git repack -Adb --write-midx &&

		test_path_is_file $midx &&
		test_path_is_missing $midx-$checksum.bitmap &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		test_commit repack-4 &&
		GIT_TEST_MULTI_PACK_INDEX=0 git repack -Adb &&

		find $objdir/pack -type f -name "multi-pack-index*" >files &&
		test_must_be_empty files
	)
'

test_expect_success '--write-midx with preferred bitmap tips' '
	git init midx-preferred-tips &&
	test_when_finished "rm -fr midx-preferred-tips" &&
	(
		cd midx-preferred-tips &&

		test_commit_bulk --message="%s" 103 &&

		git log --format="%H" >commits.raw &&
		sort <commits.raw >commits &&

		git log --format="create refs/tags/%s/%s %H" HEAD >refs &&
		git update-ref --stdin <refs &&

		git repack --write-midx --write-bitmap-index &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&

		test-tool bitmap list-commits | sort >bitmaps &&
		comm -13 bitmaps commits >before &&
		test_line_count = 1 before &&

		rm -fr $midx-$(midx_checksum $objdir).bitmap &&
		rm -fr $midx &&

		# instead of constructing the snapshot ourselves (c.f., the test
		# "write a bitmap with --refs-snapshot (preferred tips)" in
		# t5326), mark the missing commit as preferred by adding it to
		# the pack.preferBitmapTips configuration.
		git for-each-ref --format="%(refname:rstrip=1)" \
			--points-at="$(cat before)" >missing &&
		git config pack.preferBitmapTips "$(cat missing)" &&
		git repack --write-midx --write-bitmap-index &&

		test-tool bitmap list-commits | sort >bitmaps &&
		comm -13 bitmaps commits >after &&

		! test_cmp before after
	)
'

# The first argument is expected to be a filename
# and that file should contain the name of a .idx
# file. Send the list of objects in that .idx file
# into stdout.
get_sorted_objects_from_pack () {
	git show-index <$(cat "$1") >raw &&
	cut -d" " -f2 raw
}

test_expect_success '--write-midx -b packs non-kept objects' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		# Create a kept pack-file
		test_commit base &&
		git repack -ad &&
		find $objdir/pack -name "*.idx" >before &&
		test_line_count = 1 before &&
		before_name=$(cat before) &&
		>${before_name%.idx}.keep &&

		# Create a non-kept pack-file
		test_commit other &&
		git repack &&

		# Create loose objects
		test_commit loose &&

		# Repack everything
		git repack --write-midx -a -b -d &&

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

test_expect_success '--write-midx removes stale pack-based bitmaps' '
       rm -fr repo &&
       git init repo &&
       test_when_finished "rm -fr repo" &&
       (
		cd repo &&
		test_commit base &&
		GIT_TEST_MULTI_PACK_INDEX=0 git repack -Ab &&

		pack_bitmap=$(ls $objdir/pack/pack-*.bitmap) &&
		test_path_is_file "$pack_bitmap" &&

		test_commit tip &&
		GIT_TEST_MULTI_PACK_INDEX=0 git repack -bm &&

		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap &&
		test_path_is_missing $pack_bitmap
       )
'

test_expect_success '--write-midx with --pack-kept-objects' '
	git init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		test_commit one &&
		test_commit two &&

		one="$(echo "one" | git pack-objects --revs $objdir/pack/pack)" &&
		two="$(echo "one..two" | git pack-objects --revs $objdir/pack/pack)" &&

		keep="$objdir/pack/pack-$one.keep" &&
		touch "$keep" &&

		git repack --write-midx --write-bitmap-index --geometric=2 -d \
			--pack-kept-objects &&

		test_path_is_file $keep &&
		test_path_is_file $midx &&
		test_path_is_file $midx-$(midx_checksum $objdir).bitmap
	)
'

test_expect_success TTY '--quiet disables progress' '
	test_terminal env GIT_PROGRESS_DELAY=0 \
		git -C midx repack -ad --quiet --write-midx 2>stderr &&
	test_must_be_empty stderr
'

test_expect_success 'clean up .tmp-* packs on error' '
	test_must_fail ok=sigpipe git \
		-c repack.cruftwindow=bogus \
		repack -ad --cruft &&
	find $objdir/pack -name '.tmp-*' >tmpfiles &&
	test_must_be_empty tmpfiles
'

test_expect_success 'repack -ad cleans up old .tmp-* packs' '
	git rev-parse HEAD >input &&
	git pack-objects $objdir/pack/.tmp-1234 <input &&
	git repack -ad &&
	find $objdir/pack -name '.tmp-*' >tmpfiles &&
	test_must_be_empty tmpfiles
'

test_expect_success 'setup for update-server-info' '
	git init update-server-info &&
	test_commit -C update-server-info message
'

test_server_info_present () {
	test_path_is_file update-server-info/.git/objects/info/packs &&
	test_path_is_file update-server-info/.git/info/refs
}

test_server_info_missing () {
	test_path_is_missing update-server-info/.git/objects/info/packs &&
	test_path_is_missing update-server-info/.git/info/refs
}

test_server_info_cleanup () {
	rm -f update-server-info/.git/objects/info/packs update-server-info/.git/info/refs &&
	test_server_info_missing
}

test_expect_success 'updates server info by default' '
	test_server_info_cleanup &&
	git -C update-server-info repack &&
	test_server_info_present
'

test_expect_success '-n skips updating server info' '
	test_server_info_cleanup &&
	git -C update-server-info repack -n &&
	test_server_info_missing
'

test_expect_success 'repack.updateServerInfo=true updates server info' '
	test_server_info_cleanup &&
	git -C update-server-info -c repack.updateServerInfo=true repack &&
	test_server_info_present
'

test_expect_success 'repack.updateServerInfo=false skips updating server info' '
	test_server_info_cleanup &&
	git -C update-server-info -c repack.updateServerInfo=false repack &&
	test_server_info_missing
'

test_expect_success '-n overrides repack.updateServerInfo=true' '
	test_server_info_cleanup &&
	git -C update-server-info -c repack.updateServerInfo=true repack -n &&
	test_server_info_missing
'

test_expect_success '--expire-to stores pruned objects (now)' '
	git init expire-to-now &&
	(
		cd expire-to-now &&

		git branch -M main &&

		test_commit base &&

		git checkout -b cruft &&
		test_commit --no-tag cruft &&

		git rev-list --objects --no-object-names main..cruft >moved.raw &&
		sort moved.raw >moved.want &&

		git rev-list --all --objects --no-object-names >expect.raw &&
		sort expect.raw >expect &&

		git checkout main &&
		git branch -D cruft &&
		git reflog expire --all --expire=all &&

		git init --bare expired.git &&
		git repack -d \
			--cruft --cruft-expiration="now" \
			--expire-to="expired.git/objects/pack/pack" &&

		expired="$(ls expired.git/objects/pack/pack-*.idx)" &&
		test_path_is_file "${expired%.idx}.mtimes" &&

		# Since the `--cruft-expiration` is "now", the effective
		# behavior is to move _all_ unreachable objects out to
		# the location in `--expire-to`.
		git show-index <$expired >expired.raw &&
		cut -d" " -f2 expired.raw | sort >expired.objects &&
		git rev-list --all --objects --no-object-names \
			>remaining.objects &&

		# ...in other words, the combined contents of this
		# repository and expired.git should be the same as the
		# set of objects we started with.
		cat expired.objects remaining.objects | sort >actual &&
		test_cmp expect actual &&

		# The "moved" objects (i.e., those in expired.git)
		# should be the same as the cruft objects which were
		# expired in the previous step.
		test_cmp moved.want expired.objects
	)
'

test_expect_success '--expire-to stores pruned objects (5.minutes.ago)' '
	git init expire-to-5.minutes.ago &&
	(
		cd expire-to-5.minutes.ago &&

		git branch -M main &&

		test_commit base &&

		# Create two classes of unreachable objects, one which
		# is older than 5 minutes (stale), and another which is
		# newer (recent).
		for kind in stale recent
		do
			git checkout -b $kind main &&
			test_commit --no-tag $kind || return 1
		done &&

		git rev-list --objects --no-object-names main..stale >in &&
		stale="$(git pack-objects $objdir/pack/pack <in)" &&
		mtime="$(test-tool chmtime --get =-600 $objdir/pack/pack-$stale.pack)" &&

		# expect holds the set of objects we expect to find in
		# this repository after repacking
		git rev-list --objects --no-object-names recent >expect.raw &&
		sort expect.raw >expect &&

		# moved.want holds the set of objects we expect to find
		# in expired.git
		git rev-list --objects --no-object-names main..stale >out &&
		sort out >moved.want &&

		git checkout main &&
		git branch -D stale recent &&
		git reflog expire --all --expire=all &&
		git prune-packed &&

		git init --bare expired.git &&
		git repack -d \
			--cruft --cruft-expiration=5.minutes.ago \
			--expire-to="expired.git/objects/pack/pack" &&

		# Some of the remaining objects in this repository are
		# unreachable, so use `cat-file --batch-all-objects`
		# instead of `rev-list` to get their names
		git cat-file --batch-all-objects --batch-check="%(objectname)" \
			>remaining.objects &&
		sort remaining.objects >actual &&
		test_cmp expect actual &&

		(
			cd expired.git &&

			expired="$(ls objects/pack/pack-*.mtimes)" &&
			test-tool pack-mtimes $(basename $expired) >out &&
			cut -d" " -f1 out | sort >../moved.got &&

			# Ensure that there are as many objects with the
			# expected mtime as were moved to expired.git.
			#
			# In other words, ensure that the recorded
			# mtimes of any moved objects was written
			# correctly.
			grep " $mtime$" out >matching &&
			test_line_count = $(wc -l <../moved.want) matching
		) &&
		test_cmp moved.want moved.got
	)
'

test_done
