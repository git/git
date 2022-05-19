#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='but pack-object

'
. ./test-lib.sh

test_expect_success 'setup' '
	rm -f .but/index* &&
	perl -e "print \"a\" x 4096;" >a &&
	perl -e "print \"b\" x 4096;" >b &&
	perl -e "print \"c\" x 4096;" >c &&
	test-tool genrandom "seed a" 2097152 >a_big &&
	test-tool genrandom "seed b" 2097152 >b_big &&
	but update-index --add a a_big b b_big c &&
	cat c >d && echo foo >>d && but update-index --add d &&
	tree=$(but write-tree) &&
	cummit=$(but cummit-tree $tree </dev/null) &&
	{
		echo $tree &&
		echo $cummit &&
		but ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
	} >obj-list &&
	{
		but diff-tree --root -p $cummit &&
		while read object
		do
			t=$(but cat-file -t $object) &&
			but cat-file $t $object || return 1
		done <obj-list
	} >expect
'

test_expect_success 'setup pack-object <stdin' '
	but init pack-object-stdin &&
	test_cummit -C pack-object-stdin one &&
	test_cummit -C pack-object-stdin two

'

test_expect_success 'pack-object <stdin parsing: basic [|--revs]' '
	cat >in <<-EOF &&
	$(but -C pack-object-stdin rev-parse one)
	EOF

	but -C pack-object-stdin pack-objects basic-stdin <in &&
	idx=$(echo pack-object-stdin/basic-stdin-*.idx) &&
	but show-index <"$idx" >actual &&
	test_line_count = 1 actual &&

	but -C pack-object-stdin pack-objects --revs basic-stdin-revs <in &&
	idx=$(echo pack-object-stdin/basic-stdin-revs-*.idx) &&
	but show-index <"$idx" >actual &&
	test_line_count = 3 actual
'

test_expect_success 'pack-object <stdin parsing: [|--revs] bad line' '
	cat >in <<-EOF &&
	$(but -C pack-object-stdin rev-parse one)
	garbage
	$(but -C pack-object-stdin rev-parse two)
	EOF

	sed "s/^> //g" >err.expect <<-EOF &&
	fatal: expected object ID, got garbage:
	>  garbage

	EOF
	test_must_fail but -C pack-object-stdin pack-objects bad-line-stdin <in 2>err.actual &&
	test_cmp err.expect err.actual &&

	cat >err.expect <<-EOF &&
	fatal: bad revision '"'"'garbage'"'"'
	EOF
	test_must_fail but -C pack-object-stdin pack-objects --revs bad-line-stdin-revs <in 2>err.actual &&
	test_cmp err.expect err.actual
'

test_expect_success 'pack-object <stdin parsing: [|--revs] empty line' '
	cat >in <<-EOF &&
	$(but -C pack-object-stdin rev-parse one)

	$(but -C pack-object-stdin rev-parse two)
	EOF

	sed -e "s/^> //g" -e "s/Z$//g" >err.expect <<-EOF &&
	fatal: expected object ID, got garbage:
	>  Z

	EOF
	test_must_fail but -C pack-object-stdin pack-objects empty-line-stdin <in 2>err.actual &&
	test_cmp err.expect err.actual &&

	but -C pack-object-stdin pack-objects --revs empty-line-stdin-revs <in &&
	idx=$(echo pack-object-stdin/empty-line-stdin-revs-*.idx) &&
	but show-index <"$idx" >actual &&
	test_line_count = 3 actual
'

test_expect_success 'pack-object <stdin parsing: [|--revs] with --stdin' '
	cat >in <<-EOF &&
	$(but -C pack-object-stdin rev-parse one)
	$(but -C pack-object-stdin rev-parse two)
	EOF

	# There is the "--stdin-packs is incompatible with --revs"
	# test below, but we should make sure that the revision.c
	# --stdin is not picked up
	cat >err.expect <<-EOF &&
	fatal: disallowed abbreviated or ambiguous option '"'"'stdin'"'"'
	EOF
	test_must_fail but -C pack-object-stdin pack-objects stdin-with-stdin-option --stdin <in 2>err.actual &&
	test_cmp err.expect err.actual &&

	test_must_fail but -C pack-object-stdin pack-objects --stdin --revs stdin-with-stdin-option-revs 2>err.actual <in &&
	test_cmp err.expect err.actual
'

test_expect_success 'pack-object <stdin parsing: --stdin-packs handles garbage' '
	cat >in <<-EOF &&
	$(but -C pack-object-stdin rev-parse one)
	$(but -C pack-object-stdin rev-parse two)
	EOF

	# That we get "two" and not "one" has to do with OID
	# ordering. It happens to be the same here under SHA-1 and
	# SHA-256. See commentary in pack-objects.c
	cat >err.expect <<-EOF &&
	fatal: could not find pack '"'"'$(but -C pack-object-stdin rev-parse two)'"'"'
	EOF
	test_must_fail but \
		-C pack-object-stdin \
		pack-objects stdin-with-stdin-option --stdin-packs \
		<in 2>err.actual &&
	test_cmp err.expect err.actual
'

# usage: check_deltas <stderr_from_pack_objects> <cmp_op> <nr_deltas>
# e.g.: check_deltas stderr -gt 0
check_deltas() {
	deltas=$(perl -lne '/delta (\d+)/ and print $1' "$1") &&
	shift &&
	if ! test "$deltas" "$@"
	then
		echo >&2 "unexpected number of deltas (compared $delta $*)"
		return 1
	fi
}

test_expect_success 'pack without delta' '
	packname_1=$(but pack-objects --progress --window=0 test-1 \
			<obj-list 2>stderr) &&
	check_deltas stderr = 0
'

test_expect_success 'pack-objects with bogus arguments' '
	test_must_fail but pack-objects --window=0 test-1 blah blah <obj-list
'

check_unpack () {
	test_when_finished "rm -rf but2" &&
	but init --bare but2 &&
	but -C but2 unpack-objects -n <"$1".pack &&
	but -C but2 unpack-objects <"$1".pack &&
	(cd .but && find objects -type f -print) |
	while read path
	do
		cmp but2/$path .but/$path || {
			echo $path differs.
			return 1
		}
	done
}

test_expect_success 'unpack without delta' '
	check_unpack test-1-${packname_1}
'

test_expect_success 'pack with REF_DELTA' '
	packname_2=$(but pack-objects --progress test-2 <obj-list 2>stderr) &&
	check_deltas stderr -gt 0
'

test_expect_success 'unpack with REF_DELTA' '
	check_unpack test-2-${packname_2}
'

test_expect_success 'pack with OFS_DELTA' '
	packname_3=$(but pack-objects --progress --delta-base-offset test-3 \
			<obj-list 2>stderr) &&
	check_deltas stderr -gt 0
'

test_expect_success 'unpack with OFS_DELTA' '
	check_unpack test-3-${packname_3}
'

test_expect_success 'compare delta flavors' '
	perl -e '\''
		defined($_ = -s $_) or die for @ARGV;
		exit 1 if $ARGV[0] <= $ARGV[1];
	'\'' test-2-$packname_2.pack test-3-$packname_3.pack
'

check_use_objects () {
	test_when_finished "rm -rf but2" &&
	but init --bare but2 &&
	cp "$1".pack "$1".idx but2/objects/pack &&
	(
		cd but2 &&
		but diff-tree --root -p $cummit &&
		while read object
		do
			t=$(but cat-file -t $object) &&
			but cat-file $t $object || exit 1
		done
	) <obj-list >current &&
	cmp expect current
}

test_expect_success 'use packed objects' '
	check_use_objects test-1-${packname_1}
'

test_expect_success 'use packed deltified (REF_DELTA) objects' '
	check_use_objects test-2-${packname_2}
'

test_expect_success 'use packed deltified (OFS_DELTA) objects' '
	check_use_objects test-3-${packname_3}
'

test_expect_success 'survive missing objects/pack directory' '
	(
		rm -fr missing-pack &&
		mkdir missing-pack &&
		cd missing-pack &&
		but init &&
		GOP=.but/objects/pack &&
		rm -fr $GOP &&
		but index-pack --stdin --keep=test <../test-3-${packname_3}.pack &&
		test -f $GOP/pack-${packname_3}.pack &&
		cmp $GOP/pack-${packname_3}.pack ../test-3-${packname_3}.pack &&
		test -f $GOP/pack-${packname_3}.idx &&
		cmp $GOP/pack-${packname_3}.idx ../test-3-${packname_3}.idx &&
		test -f $GOP/pack-${packname_3}.keep
	)
'

test_expect_success \
    'verify pack' \
    'but verify-pack	test-1-${packname_1}.idx \
			test-2-${packname_2}.idx \
			test-3-${packname_3}.idx'

test_expect_success \
    'verify pack -v' \
    'but verify-pack -v	test-1-${packname_1}.idx \
			test-2-${packname_2}.idx \
			test-3-${packname_3}.idx'

test_expect_success \
    'verify-pack catches mismatched .idx and .pack files' \
    'cat test-1-${packname_1}.idx >test-3.idx &&
     cat test-2-${packname_2}.pack >test-3.pack &&
     if but verify-pack test-3.idx
     then false
     else :;
     fi'

test_expect_success \
    'verify-pack catches a corrupted pack signature' \
    'cat test-1-${packname_1}.pack >test-3.pack &&
     echo | dd of=test-3.pack count=1 bs=1 conv=notrunc seek=2 &&
     if but verify-pack test-3.idx
     then false
     else :;
     fi'

test_expect_success \
    'verify-pack catches a corrupted pack version' \
    'cat test-1-${packname_1}.pack >test-3.pack &&
     echo | dd of=test-3.pack count=1 bs=1 conv=notrunc seek=7 &&
     if but verify-pack test-3.idx
     then false
     else :;
     fi'

test_expect_success \
    'verify-pack catches a corrupted type/size of the 1st packed object data' \
    'cat test-1-${packname_1}.pack >test-3.pack &&
     echo | dd of=test-3.pack count=1 bs=1 conv=notrunc seek=12 &&
     if but verify-pack test-3.idx
     then false
     else :;
     fi'

test_expect_success \
    'verify-pack catches a corrupted sum of the index file itself' \
    'l=$(wc -c <test-3.idx) &&
     l=$(expr $l - 20) &&
     cat test-1-${packname_1}.pack >test-3.pack &&
     printf "%20s" "" | dd of=test-3.idx count=20 bs=1 conv=notrunc seek=$l &&
     if but verify-pack test-3.pack
     then false
     else :;
     fi'

test_expect_success \
    'build pack index for an existing pack' \
    'cat test-1-${packname_1}.pack >test-3.pack &&
     but index-pack -o tmp.idx test-3.pack &&
     cmp tmp.idx test-1-${packname_1}.idx &&

     but index-pack --promisor=message test-3.pack &&
     cmp test-3.idx test-1-${packname_1}.idx &&
     echo message >expect &&
     test_cmp expect test-3.promisor &&

     cat test-2-${packname_2}.pack >test-3.pack &&
     but index-pack -o tmp.idx test-2-${packname_2}.pack &&
     cmp tmp.idx test-2-${packname_2}.idx &&

     but index-pack test-3.pack &&
     cmp test-3.idx test-2-${packname_2}.idx &&

     cat test-3-${packname_3}.pack >test-3.pack &&
     but index-pack -o tmp.idx test-3-${packname_3}.pack &&
     cmp tmp.idx test-3-${packname_3}.idx &&

     but index-pack test-3.pack &&
     cmp test-3.idx test-3-${packname_3}.idx &&

     cat test-1-${packname_1}.pack >test-4.pack &&
     rm -f test-4.keep &&
     but index-pack --keep=why test-4.pack &&
     cmp test-1-${packname_1}.idx test-4.idx &&
     test -f test-4.keep &&

     :'

test_expect_success 'unpacking with --strict' '

	for j in a b c d e f g
	do
		for i in 0 1 2 3 4 5 6 7 8 9
		do
			o=$(echo $j$i | but hash-object -w --stdin) &&
			echo "100644 $o	0 $j$i" || return 1
		done
	done >LIST &&
	rm -f .but/index &&
	but update-index --index-info <LIST &&
	LIST=$(but write-tree) &&
	rm -f .but/index &&
	head -n 10 LIST | but update-index --index-info &&
	LI=$(but write-tree) &&
	rm -f .but/index &&
	tail -n 10 LIST | but update-index --index-info &&
	ST=$(but write-tree) &&
	but rev-list --objects "$LIST" "$LI" "$ST" >actual &&
	PACK5=$( but pack-objects test-5 <actual ) &&
	PACK6=$( test_write_lines "$LIST" "$LI" "$ST" | but pack-objects test-6 ) &&
	test_create_repo test-5 &&
	(
		cd test-5 &&
		but unpack-objects --strict <../test-5-$PACK5.pack &&
		but ls-tree -r $LIST &&
		but ls-tree -r $LI &&
		but ls-tree -r $ST
	) &&
	test_create_repo test-6 &&
	(
		# tree-only into empty repo -- many unreachables
		cd test-6 &&
		test_must_fail but unpack-objects --strict <../test-6-$PACK6.pack
	) &&
	(
		# already populated -- no unreachables
		cd test-5 &&
		but unpack-objects --strict <../test-6-$PACK6.pack
	)
'

test_expect_success 'index-pack with --strict' '

	for j in a b c d e f g
	do
		for i in 0 1 2 3 4 5 6 7 8 9
		do
			o=$(echo $j$i | but hash-object -w --stdin) &&
			echo "100644 $o	0 $j$i" || return 1
		done
	done >LIST &&
	rm -f .but/index &&
	but update-index --index-info <LIST &&
	LIST=$(but write-tree) &&
	rm -f .but/index &&
	head -n 10 LIST | but update-index --index-info &&
	LI=$(but write-tree) &&
	rm -f .but/index &&
	tail -n 10 LIST | but update-index --index-info &&
	ST=$(but write-tree) &&
	but rev-list --objects "$LIST" "$LI" "$ST" >actual &&
	PACK5=$( but pack-objects test-5 <actual ) &&
	PACK6=$( test_write_lines "$LIST" "$LI" "$ST" | but pack-objects test-6 ) &&
	test_create_repo test-7 &&
	(
		cd test-7 &&
		but index-pack --strict --stdin <../test-5-$PACK5.pack &&
		but ls-tree -r $LIST &&
		but ls-tree -r $LI &&
		but ls-tree -r $ST
	) &&
	test_create_repo test-8 &&
	(
		# tree-only into empty repo -- many unreachables
		cd test-8 &&
		test_must_fail but index-pack --strict --stdin <../test-6-$PACK6.pack
	) &&
	(
		# already populated -- no unreachables
		cd test-7 &&
		but index-pack --strict --stdin <../test-6-$PACK6.pack
	)
'

test_expect_success 'honor pack.packSizeLimit' '
	but config pack.packSizeLimit 3m &&
	packname_10=$(but pack-objects test-10 <obj-list) &&
	test 2 = $(ls test-10-*.pack | wc -l)
'

test_expect_success 'verify resulting packs' '
	but verify-pack test-10-*.pack
'

test_expect_success 'tolerate packsizelimit smaller than biggest object' '
	but config pack.packSizeLimit 1 &&
	packname_11=$(but pack-objects test-11 <obj-list) &&
	test 5 = $(ls test-11-*.pack | wc -l)
'

test_expect_success 'verify resulting packs' '
	but verify-pack test-11-*.pack
'

test_expect_success 'set up pack for non-repo tests' '
	# make sure we have a pack with no matching index file
	cp test-1-*.pack foo.pack
'

test_expect_success 'index-pack --stdin complains of non-repo' '
	nonbut test_must_fail but index-pack --object-format=$(test_oid algo) --stdin <foo.pack &&
	test_path_is_missing non-repo/.but
'

test_expect_success 'index-pack <pack> works in non-repo' '
	nonbut but index-pack --object-format=$(test_oid algo) ../foo.pack &&
	test_path_is_file foo.idx
'

test_expect_success 'index-pack --strict <pack> works in non-repo' '
	rm -f foo.idx &&
	nonbut but index-pack --strict --object-format=$(test_oid algo) ../foo.pack &&
	test_path_is_file foo.idx
'

test_expect_success !PTHREADS,!FAIL_PREREQS \
	'index-pack --threads=N or pack.threads=N warns when no pthreads' '
	test_must_fail but index-pack --threads=2 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 1 warnings &&
	grep -F "no threads support, ignoring --threads=2" err &&

	test_must_fail but -c pack.threads=2 index-pack 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 1 warnings &&
	grep -F "no threads support, ignoring pack.threads" err &&

	test_must_fail but -c pack.threads=2 index-pack --threads=4 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 2 warnings &&
	grep -F "no threads support, ignoring --threads=4" err &&
	grep -F "no threads support, ignoring pack.threads" err
'

test_expect_success !PTHREADS,!FAIL_PREREQS \
	'pack-objects --threads=N or pack.threads=N warns when no pthreads' '
	but pack-objects --threads=2 --stdout --all </dev/null >/dev/null 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 1 warnings &&
	grep -F "no threads support, ignoring --threads" err &&

	but -c pack.threads=2 pack-objects --stdout --all </dev/null >/dev/null 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 1 warnings &&
	grep -F "no threads support, ignoring pack.threads" err &&

	but -c pack.threads=2 pack-objects --threads=4 --stdout --all </dev/null >/dev/null 2>err &&
	grep ^warning: err >warnings &&
	test_line_count = 2 warnings &&
	grep -F "no threads support, ignoring --threads" err &&
	grep -F "no threads support, ignoring pack.threads" err
'

test_expect_success 'pack-objects in too-many-packs mode' '
	BUT_TEST_FULL_IN_PACK_ARRAY=1 but repack -ad &&
	but fsck
'

test_expect_success 'setup: fake a SHA1 hash collision' '
	but init corrupt &&
	(
		cd corrupt &&
		long_a=$(but hash-object -w ../a | sed -e "s!^..!&/!") &&
		long_b=$(but hash-object -w ../b | sed -e "s!^..!&/!") &&
		test -f	.but/objects/$long_b &&
		cp -f	.but/objects/$long_a \
			.but/objects/$long_b
	)
'

test_expect_success 'make sure index-pack detects the SHA1 collision' '
	(
		cd corrupt &&
		test_must_fail but index-pack -o ../bad.idx ../test-3.pack 2>msg &&
		test_i18ngrep "SHA1 COLLISION FOUND" msg
	)
'

test_expect_success 'make sure index-pack detects the SHA1 collision (large blobs)' '
	(
		cd corrupt &&
		test_must_fail but -c core.bigfilethreshold=1 index-pack -o ../bad.idx ../test-3.pack 2>msg &&
		test_i18ngrep "SHA1 COLLISION FOUND" msg
	)
'

test_expect_success 'prefetch objects' '
	rm -rf server client &&

	but init server &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server protocol.version 2 &&

	echo one >server/one &&
	but -C server add one &&
	but -C server cummit -m one &&
	but -C server branch one_branch &&

	echo two_a >server/two_a &&
	echo two_b >server/two_b &&
	but -C server add two_a two_b &&
	but -C server cummit -m two &&

	echo three >server/three &&
	but -C server add three &&
	but -C server cummit -m three &&
	but -C server branch three_branch &&

	# Clone, fetch "two" with blobs excluded, and re-push it. This requires
	# the client to have the blobs of "two" - verify that these are
	# prefetched in one batch.
	but clone --filter=blob:none --single-branch -b one_branch \
		"file://$(pwd)/server" client &&
	test_config -C client protocol.version 2 &&
	TWO=$(but -C server rev-parse three_branch^) &&
	but -C client fetch --filter=blob:none origin "$TWO" &&
	BUT_TRACE_PACKET=$(pwd)/trace but -C client push origin "$TWO":refs/heads/two_branch &&
	grep "fetch> done" trace >donelines &&
	test_line_count = 1 donelines
'

test_expect_success 'setup for --stdin-packs tests' '
	but init stdin-packs &&
	(
		cd stdin-packs &&

		test_cummit A &&
		test_cummit B &&
		test_cummit C &&

		for id in A B C
		do
			but pack-objects .but/objects/pack/pack-$id \
				--incremental --revs <<-EOF || exit 1
			refs/tags/$id
			EOF
		done &&

		ls -la .but/objects/pack
	)
'

test_expect_success '--stdin-packs with excluded packs' '
	(
		cd stdin-packs &&

		PACK_A="$(basename .but/objects/pack/pack-A-*.pack)" &&
		PACK_B="$(basename .but/objects/pack/pack-B-*.pack)" &&
		PACK_C="$(basename .but/objects/pack/pack-C-*.pack)" &&

		but pack-objects test --stdin-packs <<-EOF &&
		$PACK_A
		^$PACK_B
		$PACK_C
		EOF

		(
			but show-index <$(ls .but/objects/pack/pack-A-*.idx) &&
			but show-index <$(ls .but/objects/pack/pack-C-*.idx)
		) >expect.raw &&
		but show-index <$(ls test-*.idx) >actual.raw &&

		cut -d" " -f2 <expect.raw | sort >expect &&
		cut -d" " -f2 <actual.raw | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--stdin-packs is incompatible with --filter' '
	(
		cd stdin-packs &&
		test_must_fail but pack-objects --stdin-packs --stdout \
			--filter=blob:none </dev/null 2>err &&
		test_i18ngrep "cannot use --filter with --stdin-packs" err
	)
'

test_expect_success '--stdin-packs is incompatible with --revs' '
	(
		cd stdin-packs &&
		test_must_fail but pack-objects --stdin-packs --revs out \
			</dev/null 2>err &&
		test_i18ngrep "cannot use internal rev list with --stdin-packs" err
	)
'

test_expect_success '--stdin-packs with loose objects' '
	(
		cd stdin-packs &&

		PACK_A="$(basename .but/objects/pack/pack-A-*.pack)" &&
		PACK_B="$(basename .but/objects/pack/pack-B-*.pack)" &&
		PACK_C="$(basename .but/objects/pack/pack-C-*.pack)" &&

		test_cummit D && # loose

		but pack-objects test2 --stdin-packs --unpacked <<-EOF &&
		$PACK_A
		^$PACK_B
		$PACK_C
		EOF

		(
			but show-index <$(ls .but/objects/pack/pack-A-*.idx) &&
			but show-index <$(ls .but/objects/pack/pack-C-*.idx) &&
			but rev-list --objects --no-object-names \
				refs/tags/C..refs/tags/D

		) >expect.raw &&
		ls -la . &&
		but show-index <$(ls test2-*.idx) >actual.raw &&

		cut -d" " -f2 <expect.raw | sort >expect &&
		cut -d" " -f2 <actual.raw | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success '--stdin-packs with broken links' '
	(
		cd stdin-packs &&

		# make an unreachable object with a bogus parent
		but cat-file -p HEAD >cummit &&
		sed "s/$(but rev-parse HEAD^)/$(test_oid zero)/" <cummit |
		but hash-object -w -t cummit --stdin >in &&

		but pack-objects .but/objects/pack/pack-D <in &&

		PACK_A="$(basename .but/objects/pack/pack-A-*.pack)" &&
		PACK_B="$(basename .but/objects/pack/pack-B-*.pack)" &&
		PACK_C="$(basename .but/objects/pack/pack-C-*.pack)" &&
		PACK_D="$(basename .but/objects/pack/pack-D-*.pack)" &&

		but pack-objects test3 --stdin-packs --unpacked <<-EOF &&
		$PACK_A
		^$PACK_B
		$PACK_C
		$PACK_D
		EOF

		(
			but show-index <$(ls .but/objects/pack/pack-A-*.idx) &&
			but show-index <$(ls .but/objects/pack/pack-C-*.idx) &&
			but show-index <$(ls .but/objects/pack/pack-D-*.idx) &&
			but rev-list --objects --no-object-names \
				refs/tags/C..refs/tags/D
		) >expect.raw &&
		but show-index <$(ls test3-*.idx) >actual.raw &&

		cut -d" " -f2 <expect.raw | sort >expect &&
		cut -d" " -f2 <actual.raw | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'negative window clamps to 0' '
	but pack-objects --progress --window=-1 neg-window <obj-list 2>stderr &&
	check_deltas stderr = 0
'

test_done
