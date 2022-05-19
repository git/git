#!/bin/sh

test_description='multi-pack-indexes'
. ./test-lib.sh

GIT_TEST_MULTI_PACK_INDEX=0
objdir=.but/objects

HASH_LEN=$(test_oid rawsz)

midx_read_expect () {
	NUM_PACKS=$1
	NUM_OBJECTS=$2
	NUM_CHUNKS=$3
	OBJECT_DIR=$4
	EXTRA_CHUNKS="$5"
	{
		cat <<-EOF &&
		header: 4d494458 1 $HASH_LEN $NUM_CHUNKS $NUM_PACKS
		chunks: pack-names oid-fanout oid-lookup object-offsets$EXTRA_CHUNKS
		num_objects: $NUM_OBJECTS
		packs:
		EOF
		if test $NUM_PACKS -ge 1
		then
			ls $OBJECT_DIR/pack/ | grep idx | sort
		fi &&
		printf "object-dir: $OBJECT_DIR\n"
	} >expect &&
	test-tool read-midx $OBJECT_DIR >actual &&
	test_cmp expect actual
}

test_expect_success 'setup' '
	test_oid_cache <<-EOF
	idxoff sha1:2999
	idxoff sha256:3739

	packnameoff sha1:652
	packnameoff sha256:940

	fanoutoff sha1:1
	fanoutoff sha256:3
	EOF
'

test_expect_success "don't write midx with no packs" '
	test_must_fail but multi-pack-index --object-dir=. write &&
	test_path_is_missing pack/multi-pack-index
'

test_expect_success SHA1 'warn if a midx contains no oid' '
	cp "$TEST_DIRECTORY"/t5319/no-objects.midx $objdir/pack/multi-pack-index &&
	test_must_fail but multi-pack-index verify &&
	rm $objdir/pack/multi-pack-index
'

generate_objects () {
	i=$1
	iii=$(printf '%03i' $i)
	{
		test-tool genrandom "bar" 200 &&
		test-tool genrandom "baz $iii" 50
	} >wide_delta_$iii &&
	{
		test-tool genrandom "foo"$i 100 &&
		test-tool genrandom "foo"$(( $i + 1 )) 100 &&
		test-tool genrandom "foo"$(( $i + 2 )) 100
	} >deep_delta_$iii &&
	{
		echo $iii &&
		test-tool genrandom "$iii" 8192
	} >file_$iii &&
	but update-index --add file_$iii deep_delta_$iii wide_delta_$iii
}

cummit_and_list_objects () {
	{
		echo 101 &&
		test-tool genrandom 100 8192;
	} >file_101 &&
	but update-index --add file_101 &&
	tree=$(but write-tree) &&
	cummit=$(but cummit-tree $tree -p HEAD</dev/null) &&
	{
		echo $tree &&
		but ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
	} >obj-list &&
	but reset --hard $cummit
}

test_expect_success 'create objects' '
	test_cummit initial &&
	for i in $(test_seq 1 5)
	do
		generate_objects $i || return 1
	done &&
	cummit_and_list_objects
'

test_expect_success 'write midx with one v1 pack' '
	pack=$(but pack-objects --index-version=1 $objdir/pack/test <obj-list) &&
	test_when_finished rm $objdir/pack/test-$pack.pack \
		$objdir/pack/test-$pack.idx $objdir/pack/multi-pack-index &&
	but multi-pack-index --object-dir=$objdir write &&
	midx_read_expect 1 18 4 $objdir
'

midx_but_two_modes () {
	but -c core.multiPackIndex=false $1 >expect &&
	but -c core.multiPackIndex=true $1 >actual &&
	if [ "$2" = "sorted" ]
	then
		sort <expect >expect.sorted &&
		mv expect.sorted expect &&
		sort <actual >actual.sorted &&
		mv actual.sorted actual
	fi &&
	test_cmp expect actual
}

compare_results_with_midx () {
	MSG=$1
	test_expect_success "check normal but operations: $MSG" '
		midx_but_two_modes "rev-list --objects --all" &&
		midx_but_two_modes "log --raw" &&
		midx_but_two_modes "count-objects --verbose" &&
		midx_but_two_modes "cat-file --batch-all-objects --batch-check" &&
		midx_but_two_modes "cat-file --batch-all-objects --batch-check --unordered" sorted
	'
}

test_expect_success 'write midx with one v2 pack' '
	but pack-objects --index-version=2,0x40 $objdir/pack/test <obj-list &&
	but multi-pack-index --object-dir=$objdir write &&
	midx_read_expect 1 18 4 $objdir
'

compare_results_with_midx "one v2 pack"

test_expect_success 'corrupt idx reports errors' '
	idx=$(test-tool read-midx $objdir | grep "\.idx\$") &&
	mv $objdir/pack/$idx backup-$idx &&
	test_when_finished "mv backup-\$idx \$objdir/pack/\$idx" &&

	# This is the minimum size for a sha-1 based .idx; this lets
	# us pass perfunctory tests, but anything that actually opens and reads
	# the idx file will complain.
	test_copy_bytes 1064 <backup-$idx >$objdir/pack/$idx &&

	but -c core.multiPackIndex=true rev-list --objects --all 2>err &&
	grep "index unavailable" err
'

test_expect_success 'add more objects' '
	for i in $(test_seq 6 10)
	do
		generate_objects $i || return 1
	done &&
	cummit_and_list_objects
'

test_expect_success 'write midx with two packs' '
	but pack-objects --index-version=1 $objdir/pack/test-2 <obj-list &&
	but multi-pack-index --object-dir=$objdir write &&
	midx_read_expect 2 34 4 $objdir
'

compare_results_with_midx "two packs"

test_expect_success 'write midx with --stdin-packs' '
	rm -fr $objdir/pack/multi-pack-index &&

	idx="$(find $objdir/pack -name "test-2-*.idx")" &&
	basename "$idx" >in &&

	but multi-pack-index write --stdin-packs <in &&

	test-tool read-midx $objdir | grep "\.idx$" >packs &&

	test_cmp packs in
'

compare_results_with_midx "mixed mode (one pack + extra)"

test_expect_success 'write progress off for redirected stderr' '
	but multi-pack-index --object-dir=$objdir write 2>err &&
	test_line_count = 0 err
'

test_expect_success 'write force progress on for stderr' '
	GIT_PROGRESS_DELAY=0 but multi-pack-index --object-dir=$objdir write --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'write with the --no-progress option' '
	GIT_PROGRESS_DELAY=0 but multi-pack-index --object-dir=$objdir write --no-progress 2>err &&
	test_line_count = 0 err
'

test_expect_success 'add more packs' '
	for j in $(test_seq 11 20)
	do
		generate_objects $j &&
		cummit_and_list_objects &&
		but pack-objects --index-version=2 $objdir/pack/test-pack <obj-list || return 1
	done
'

compare_results_with_midx "mixed mode (two packs + extra)"

test_expect_success 'write midx with twelve packs' '
	but multi-pack-index --object-dir=$objdir write &&
	midx_read_expect 12 74 4 $objdir
'

compare_results_with_midx "twelve packs"

test_expect_success 'multi-pack-index *.rev cleanup with --object-dir' '
	but init repo &&
	but clone -s repo alternate &&

	test_when_finished "rm -rf repo alternate" &&

	(
		cd repo &&
		test_cummit base &&
		but repack -d
	) &&

	ours="alternate/.but/objects/pack/multi-pack-index-123.rev" &&
	theirs="repo/.but/objects/pack/multi-pack-index-abc.rev" &&
	touch "$ours" "$theirs" &&

	(
		cd alternate &&
		but multi-pack-index --object-dir ../repo/.but/objects write
	) &&

	# writing a midx in "repo" should not remove the .rev file in the
	# alternate
	test_path_is_file repo/.but/objects/pack/multi-pack-index &&
	test_path_is_file $ours &&
	test_path_is_missing $theirs
'

test_expect_success 'warn on improper hash version' '
	but init --object-format=sha1 sha1 &&
	(
		cd sha1 &&
		but config core.multiPackIndex true &&
		test_cummit 1 &&
		but repack -a &&
		but multi-pack-index write &&
		mv .but/objects/pack/multi-pack-index ../mpi-sha1
	) &&
	but init --object-format=sha256 sha256 &&
	(
		cd sha256 &&
		but config core.multiPackIndex true &&
		test_cummit 1 &&
		but repack -a &&
		but multi-pack-index write &&
		mv .but/objects/pack/multi-pack-index ../mpi-sha256
	) &&
	(
		cd sha1 &&
		mv ../mpi-sha256 .but/objects/pack/multi-pack-index &&
		but log -1 2>err &&
		test_i18ngrep "multi-pack-index hash version 2 does not match version 1" err
	) &&
	(
		cd sha256 &&
		mv ../mpi-sha1 .but/objects/pack/multi-pack-index &&
		but log -1 2>err &&
		test_i18ngrep "multi-pack-index hash version 1 does not match version 2" err
	)
'

test_expect_success 'midx picks objects from preferred pack' '
	test_when_finished rm -rf preferred.but &&
	but init --bare preferred.but &&
	(
		cd preferred.but &&

		a=$(echo "a" | but hash-object -w --stdin) &&
		b=$(echo "b" | but hash-object -w --stdin) &&
		c=$(echo "c" | but hash-object -w --stdin) &&

		# Set up two packs, duplicating the object "B" at different
		# offsets.
		#
		# Note that the "BC" pack (the one we choose as preferred) sorts
		# lexically after the "AB" pack, meaning that omitting the
		# --preferred-pack argument would cause this test to fail (since
		# the MIDX code would select the copy of "b" in the "AB" pack).
		but pack-objects objects/pack/test-AB <<-EOF &&
		$a
		$b
		EOF
		bc=$(but pack-objects objects/pack/test-BC <<-EOF
		$b
		$c
		EOF
		) &&

		but multi-pack-index --object-dir=objects \
			write --preferred-pack=test-BC-$bc.idx 2>err &&
		test_must_be_empty err &&

		test-tool read-midx --show-objects objects >out &&

		ofs=$(but show-index <objects/pack/test-BC-$bc.idx | grep $b |
			cut -d" " -f1) &&
		printf "%s %s\tobjects/pack/test-BC-%s.pack\n" \
			"$b" "$ofs" "$bc" >expect &&
		grep ^$b out >actual &&

		test_cmp expect actual
	)
'

test_expect_success 'preferred packs must be non-empty' '
	test_when_finished rm -rf preferred.but &&
	but init preferred.but &&
	(
		cd preferred.but &&

		test_cummit base &&
		but repack -ad &&

		empty="$(but pack-objects $objdir/pack/pack </dev/null)" &&

		test_must_fail but multi-pack-index write \
			--preferred-pack=pack-$empty.pack 2>err &&
		grep "with no objects" err
	)
'

test_expect_success 'verify multi-pack-index success' '
	but multi-pack-index verify --object-dir=$objdir
'

test_expect_success 'verify progress off for redirected stderr' '
	but multi-pack-index verify --object-dir=$objdir 2>err &&
	test_line_count = 0 err
'

test_expect_success 'verify force progress on for stderr' '
	but multi-pack-index verify --object-dir=$objdir --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'verify with the --no-progress option' '
	but multi-pack-index verify --object-dir=$objdir --no-progress 2>err &&
	test_line_count = 0 err
'

# usage: corrupt_midx_and_verify <pos> <data> <objdir> <string>
corrupt_midx_and_verify() {
	POS=$1 &&
	DATA="${2:-\0}" &&
	OBJDIR=$3 &&
	GREPSTR="$4" &&
	COMMAND="$5" &&
	if test -z "$COMMAND"
	then
		COMMAND="but multi-pack-index verify --object-dir=$OBJDIR"
	fi &&
	FILE=$OBJDIR/pack/multi-pack-index &&
	chmod a+w $FILE &&
	test_when_finished mv midx-backup $FILE &&
	cp $FILE midx-backup &&
	printf "$DATA" | dd of="$FILE" bs=1 seek="$POS" conv=notrunc &&
	test_must_fail $COMMAND 2>test_err &&
	grep -v "^+" test_err >err &&
	test_i18ngrep "$GREPSTR" err
}

test_expect_success 'verify bad signature' '
	corrupt_midx_and_verify 0 "\00" $objdir \
		"multi-pack-index signature"
'

NUM_OBJECTS=74
MIDX_BYTE_VERSION=4
MIDX_BYTE_OID_VERSION=5
MIDX_BYTE_CHUNK_COUNT=6
MIDX_HEADER_SIZE=12
MIDX_BYTE_CHUNK_ID=$MIDX_HEADER_SIZE
MIDX_BYTE_CHUNK_OFFSET=$(($MIDX_HEADER_SIZE + 4))
MIDX_NUM_CHUNKS=5
MIDX_CHUNK_LOOKUP_WIDTH=12
MIDX_OFFSET_PACKNAMES=$(($MIDX_HEADER_SIZE + \
			 $MIDX_NUM_CHUNKS * $MIDX_CHUNK_LOOKUP_WIDTH))
MIDX_BYTE_PACKNAME_ORDER=$(($MIDX_OFFSET_PACKNAMES + 2))
MIDX_OFFSET_OID_FANOUT=$(($MIDX_OFFSET_PACKNAMES + $(test_oid packnameoff)))
MIDX_OID_FANOUT_WIDTH=4
MIDX_BYTE_OID_FANOUT_ORDER=$((MIDX_OFFSET_OID_FANOUT + 250 * $MIDX_OID_FANOUT_WIDTH + $(test_oid fanoutoff)))
MIDX_OFFSET_OID_LOOKUP=$(($MIDX_OFFSET_OID_FANOUT + 256 * $MIDX_OID_FANOUT_WIDTH))
MIDX_BYTE_OID_LOOKUP=$(($MIDX_OFFSET_OID_LOOKUP + 16 * $HASH_LEN))
MIDX_OFFSET_OBJECT_OFFSETS=$(($MIDX_OFFSET_OID_LOOKUP + $NUM_OBJECTS * $HASH_LEN))
MIDX_OFFSET_WIDTH=8
MIDX_BYTE_PACK_INT_ID=$(($MIDX_OFFSET_OBJECT_OFFSETS + 16 * $MIDX_OFFSET_WIDTH + 2))
MIDX_BYTE_OFFSET=$(($MIDX_OFFSET_OBJECT_OFFSETS + 16 * $MIDX_OFFSET_WIDTH + 6))

test_expect_success 'verify bad version' '
	corrupt_midx_and_verify $MIDX_BYTE_VERSION "\00" $objdir \
		"multi-pack-index version"
'

test_expect_success 'verify bad OID version' '
	corrupt_midx_and_verify $MIDX_BYTE_OID_VERSION "\03" $objdir \
		"hash version"
'

test_expect_success 'verify truncated chunk count' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_COUNT "\01" $objdir \
		"final chunk has non-zero id"
'

test_expect_success 'verify extended chunk count' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_COUNT "\07" $objdir \
		"terminating chunk id appears earlier than expected"
'

test_expect_success 'verify missing required chunk' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_ID "\01" $objdir \
		"missing required"
'

test_expect_success 'verify invalid chunk offset' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_OFFSET "\01" $objdir \
		"improper chunk offset(s)"
'

test_expect_success 'verify packnames out of order' '
	corrupt_midx_and_verify $MIDX_BYTE_PACKNAME_ORDER "z" $objdir \
		"pack names out of order"
'

test_expect_success 'verify packnames out of order' '
	corrupt_midx_and_verify $MIDX_BYTE_PACKNAME_ORDER "a" $objdir \
		"failed to load pack"
'

test_expect_success 'verify oid fanout out of order' '
	corrupt_midx_and_verify $MIDX_BYTE_OID_FANOUT_ORDER "\01" $objdir \
		"oid fanout out of order"
'

test_expect_success 'verify oid lookup out of order' '
	corrupt_midx_and_verify $MIDX_BYTE_OID_LOOKUP "\00" $objdir \
		"oid lookup out of order"
'

test_expect_success 'verify incorrect pack-int-id' '
	corrupt_midx_and_verify $MIDX_BYTE_PACK_INT_ID "\07" $objdir \
		"bad pack-int-id"
'

test_expect_success 'verify incorrect offset' '
	corrupt_midx_and_verify $MIDX_BYTE_OFFSET "\377" $objdir \
		"incorrect object offset"
'

test_expect_success 'but-fsck incorrect offset' '
	corrupt_midx_and_verify $MIDX_BYTE_OFFSET "\377" $objdir \
		"incorrect object offset" \
		"but -c core.multiPackIndex=true fsck" &&
	test_unconfig core.multiPackIndex &&
	test_must_fail but fsck &&
	but -c core.multiPackIndex=false fsck
'

test_expect_success 'corrupt MIDX is not reused' '
	corrupt_midx_and_verify $MIDX_BYTE_OFFSET "\377" $objdir \
		"incorrect object offset" &&
	but multi-pack-index write 2>err &&
	test_i18ngrep checksum.mismatch err &&
	but multi-pack-index verify
'

test_expect_success 'verify incorrect checksum' '
	pos=$(($(wc -c <$objdir/pack/multi-pack-index) - 10)) &&
	corrupt_midx_and_verify $pos \
		"\377\377\377\377\377\377\377\377\377\377" \
		$objdir "incorrect checksum"
'

test_expect_success 'repack progress off for redirected stderr' '
	GIT_PROGRESS_DELAY=0 but multi-pack-index --object-dir=$objdir repack 2>err &&
	test_line_count = 0 err
'

test_expect_success 'repack force progress on for stderr' '
	GIT_PROGRESS_DELAY=0 but multi-pack-index --object-dir=$objdir repack --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'repack with the --no-progress option' '
	GIT_PROGRESS_DELAY=0 but multi-pack-index --object-dir=$objdir repack --no-progress 2>err &&
	test_line_count = 0 err
'

test_expect_success 'repack removes multi-pack-index when deleting packs' '
	test_path_is_file $objdir/pack/multi-pack-index &&
	# Set GIT_TEST_MULTI_PACK_INDEX to 0 to avoid writing a new
	# multi-pack-index after repacking, but set "core.multiPackIndex" to
	# true so that "but repack" can read the existing MIDX.
	GIT_TEST_MULTI_PACK_INDEX=0 but -c core.multiPackIndex repack -adf &&
	test_path_is_missing $objdir/pack/multi-pack-index
'

test_expect_success 'repack preserves multi-pack-index when creating packs' '
	but init preserve &&
	test_when_finished "rm -fr preserve" &&
	(
		cd preserve &&
		packdir=.but/objects/pack &&
		midx=$packdir/multi-pack-index &&

		test_cummit 1 &&
		pack1=$(but pack-objects --all $packdir/pack) &&
		touch $packdir/pack-$pack1.keep &&
		test_cummit 2 &&
		pack2=$(but pack-objects --revs $packdir/pack) &&
		touch $packdir/pack-$pack2.keep &&

		but multi-pack-index write &&
		cp $midx $midx.bak &&

		cat >pack-input <<-EOF &&
		HEAD
		^HEAD~1
		EOF
		test_cummit 3 &&
		pack3=$(but pack-objects --revs $packdir/pack <pack-input) &&
		test_cummit 4 &&
		pack4=$(but pack-objects --revs $packdir/pack <pack-input) &&

		GIT_TEST_MULTI_PACK_INDEX=0 but -c core.multiPackIndex repack -ad &&
		ls -la $packdir &&
		test_path_is_file $packdir/pack-$pack1.pack &&
		test_path_is_file $packdir/pack-$pack2.pack &&
		test_path_is_missing $packdir/pack-$pack3.pack &&
		test_path_is_missing $packdir/pack-$pack4.pack &&
		test_cmp_bin $midx.bak $midx
	)
'

compare_results_with_midx "after repack"

test_expect_success 'multi-pack-index and pack-bitmap' '
	GIT_TEST_MULTI_PACK_INDEX_WRITE_BITMAP=0 \
		but -c repack.writeBitmaps=true repack -ad &&
	but multi-pack-index write &&
	but rev-list --test-bitmap HEAD
'

test_expect_success 'multi-pack-index and alternates' '
	but init --bare alt.but &&
	echo $(pwd)/alt.but/objects >.but/objects/info/alternates &&
	echo content1 >file1 &&
	altblob=$(GIT_DIR=alt.but but hash-object -w file1) &&
	but cat-file blob $altblob &&
	but rev-list --all
'

compare_results_with_midx "with alternate (local midx)"

test_expect_success 'multi-pack-index in an alternate' '
	mv .but/objects/pack/* alt.but/objects/pack &&
	test_cummit add_local_objects &&
	but repack --local &&
	but multi-pack-index write &&
	midx_read_expect 1 3 4 $objdir &&
	but reset --hard HEAD~1 &&
	rm -f .but/objects/pack/*
'

compare_results_with_midx "with alternate (remote midx)"

# usage: corrupt_data <file> <pos> [<data>]
corrupt_data () {
	file=$1
	pos=$2
	data="${3:-\0}"
	printf "$data" | dd of="$file" bs=1 seek="$pos" conv=notrunc
}

# Force 64-bit offsets by manipulating the idx file.
# This makes the IDX file _incorrect_ so be careful to clean up after!
test_expect_success 'force some 64-bit offsets with pack-objects' '
	mkdir objects64 &&
	mkdir objects64/pack &&
	for i in $(test_seq 1 11)
	do
		generate_objects 11 || return 1
	done &&
	cummit_and_list_objects &&
	pack64=$(but pack-objects --index-version=2,0x40 objects64/pack/test-64 <obj-list) &&
	idx64=objects64/pack/test-64-$pack64.idx &&
	chmod u+w $idx64 &&
	corrupt_data $idx64 $(test_oid idxoff) "\02" &&
	# objects64 is not a real repository, but can serve as an alternate
	# anyway so we can write a MIDX into it
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&
		( cd ../objects64 && pwd ) >.but/objects/info/alternates &&
		midx64=$(but multi-pack-index --object-dir=../objects64 write)
	) &&
	midx_read_expect 1 63 5 objects64 " large-offsets"
'

test_expect_success 'verify multi-pack-index with 64-bit offsets' '
	but multi-pack-index verify --object-dir=objects64
'

NUM_OBJECTS=63
MIDX_OFFSET_OID_FANOUT=$((MIDX_OFFSET_PACKNAMES + 54))
MIDX_OFFSET_OID_LOOKUP=$((MIDX_OFFSET_OID_FANOUT + 256 * $MIDX_OID_FANOUT_WIDTH))
MIDX_OFFSET_OBJECT_OFFSETS=$(($MIDX_OFFSET_OID_LOOKUP + $NUM_OBJECTS * $HASH_LEN))
MIDX_OFFSET_LARGE_OFFSETS=$(($MIDX_OFFSET_OBJECT_OFFSETS + $NUM_OBJECTS * $MIDX_OFFSET_WIDTH))
MIDX_BYTE_LARGE_OFFSET=$(($MIDX_OFFSET_LARGE_OFFSETS + 3))

test_expect_success 'verify incorrect 64-bit offset' '
	corrupt_midx_and_verify $MIDX_BYTE_LARGE_OFFSET "\07" objects64 \
		"incorrect object offset"
'

test_expect_success 'setup expire tests' '
	mkdir dup &&
	(
		cd dup &&
		but init &&
		test-tool genrandom "data" 4096 >large_file.txt &&
		but update-index --add large_file.txt &&
		for i in $(test_seq 1 20)
		do
			test_cummit $i || exit 1
		done &&
		but branch A HEAD &&
		but branch B HEAD~8 &&
		but branch C HEAD~13 &&
		but branch D HEAD~16 &&
		but branch E HEAD~18 &&
		but pack-objects --revs .but/objects/pack/pack-A <<-EOF &&
		refs/heads/A
		^refs/heads/B
		EOF
		but pack-objects --revs .but/objects/pack/pack-B <<-EOF &&
		refs/heads/B
		^refs/heads/C
		EOF
		but pack-objects --revs .but/objects/pack/pack-C <<-EOF &&
		refs/heads/C
		^refs/heads/D
		EOF
		but pack-objects --revs .but/objects/pack/pack-D <<-EOF &&
		refs/heads/D
		^refs/heads/E
		EOF
		but pack-objects --revs .but/objects/pack/pack-E <<-EOF &&
		refs/heads/E
		EOF
		but multi-pack-index write &&
		cp -r .but/objects/pack .but/objects/pack-backup
	)
'

test_expect_success 'expire does not remove any packs' '
	(
		cd dup &&
		ls .but/objects/pack >expect &&
		but multi-pack-index expire &&
		ls .but/objects/pack >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'expire progress off for redirected stderr' '
	(
		cd dup &&
		but multi-pack-index expire 2>err &&
		test_line_count = 0 err
	)
'

test_expect_success 'expire force progress on for stderr' '
	(
		cd dup &&
		GIT_PROGRESS_DELAY=0 but multi-pack-index expire --progress 2>err &&
		test_file_not_empty err
	)
'

test_expect_success 'expire with the --no-progress option' '
	(
		cd dup &&
		GIT_PROGRESS_DELAY=0 but multi-pack-index expire --no-progress 2>err &&
		test_line_count = 0 err
	)
'

test_expect_success 'expire removes unreferenced packs' '
	(
		cd dup &&
		but pack-objects --revs .but/objects/pack/pack-combined <<-EOF &&
		refs/heads/A
		^refs/heads/C
		EOF
		but multi-pack-index write &&
		ls .but/objects/pack | grep -v -e pack-[AB] >expect &&
		but multi-pack-index expire &&
		ls .but/objects/pack >actual &&
		test_cmp expect actual &&
		ls .but/objects/pack/ | grep idx >expect-idx &&
		test-tool read-midx .but/objects | grep idx >actual-midx &&
		test_cmp expect-idx actual-midx &&
		but multi-pack-index verify &&
		but fsck
	)
'

test_expect_success 'repack with minimum size does not alter existing packs' '
	(
		cd dup &&
		rm -rf .but/objects/pack &&
		mv .but/objects/pack-backup .but/objects/pack &&
		test-tool chmtime =-5 .but/objects/pack/pack-D* &&
		test-tool chmtime =-4 .but/objects/pack/pack-C* &&
		test-tool chmtime =-3 .but/objects/pack/pack-B* &&
		test-tool chmtime =-2 .but/objects/pack/pack-A* &&
		ls .but/objects/pack >expect &&
		MINSIZE=$(test-tool path-utils file-size .but/objects/pack/*pack | sort -n | head -n 1) &&
		but multi-pack-index repack --batch-size=$MINSIZE &&
		ls .but/objects/pack >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'repack respects repack.packKeptObjects=false' '
	test_when_finished rm -f dup/.but/objects/pack/*keep &&
	(
		cd dup &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 5 idx-list &&
		ls .but/objects/pack/*.pack | sed "s/\.pack/.keep/" >keep-list &&
		test_line_count = 5 keep-list &&
		for keep in $(cat keep-list)
		do
			touch $keep || return 1
		done &&
		but multi-pack-index repack --batch-size=0 &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 5 idx-list &&
		test-tool read-midx .but/objects | grep idx >midx-list &&
		test_line_count = 5 midx-list &&
		THIRD_SMALLEST_SIZE=$(test-tool path-utils file-size .but/objects/pack/*pack | sort -n | sed -n 3p) &&
		BATCH_SIZE=$((THIRD_SMALLEST_SIZE + 1)) &&
		but multi-pack-index repack --batch-size=$BATCH_SIZE &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 5 idx-list &&
		test-tool read-midx .but/objects | grep idx >midx-list &&
		test_line_count = 5 midx-list
	)
'

test_expect_success 'repack creates a new pack' '
	(
		cd dup &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 5 idx-list &&
		THIRD_SMALLEST_SIZE=$(test-tool path-utils file-size .but/objects/pack/*pack | sort -n | head -n 3 | tail -n 1) &&
		BATCH_SIZE=$(($THIRD_SMALLEST_SIZE + 1)) &&
		but multi-pack-index repack --batch-size=$BATCH_SIZE &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 6 idx-list &&
		test-tool read-midx .but/objects | grep idx >midx-list &&
		test_line_count = 6 midx-list
	)
'

test_expect_success 'expire removes repacked packs' '
	(
		cd dup &&
		ls -al .but/objects/pack/*pack &&
		ls -S .but/objects/pack/*pack | head -n 4 >expect &&
		but multi-pack-index expire &&
		ls -S .but/objects/pack/*pack >actual &&
		test_cmp expect actual &&
		test-tool read-midx .but/objects | grep idx >midx-list &&
		test_line_count = 4 midx-list
	)
'

test_expect_success 'expire works when adding new packs' '
	(
		cd dup &&
		but pack-objects --revs .but/objects/pack/pack-combined <<-EOF &&
		refs/heads/A
		^refs/heads/B
		EOF
		but pack-objects --revs .but/objects/pack/pack-combined <<-EOF &&
		refs/heads/B
		^refs/heads/C
		EOF
		but pack-objects --revs .but/objects/pack/pack-combined <<-EOF &&
		refs/heads/C
		^refs/heads/D
		EOF
		but multi-pack-index write &&
		but pack-objects --revs .but/objects/pack/a-pack <<-EOF &&
		refs/heads/D
		^refs/heads/E
		EOF
		but multi-pack-index write &&
		but pack-objects --revs .but/objects/pack/z-pack <<-EOF &&
		refs/heads/E
		EOF
		but multi-pack-index expire &&
		ls .but/objects/pack/ | grep idx >expect &&
		test-tool read-midx .but/objects | grep idx >actual &&
		test_cmp expect actual &&
		but multi-pack-index verify
	)
'

test_expect_success 'expire respects .keep files' '
	(
		cd dup &&
		but pack-objects --revs .but/objects/pack/pack-all <<-EOF &&
		refs/heads/A
		EOF
		but multi-pack-index write &&
		PACKA=$(ls .but/objects/pack/a-pack*\.pack | sed s/\.pack\$//) &&
		touch $PACKA.keep &&
		but multi-pack-index expire &&
		test_path_is_file $PACKA.idx &&
		test_path_is_file $PACKA.keep &&
		test_path_is_file $PACKA.pack &&
		test-tool read-midx .but/objects | grep idx >midx-list &&
		test_line_count = 2 midx-list
	)
'

test_expect_success 'repack --batch-size=0 repacks everything' '
	cp -r dup dup2 &&
	(
		cd dup &&
		rm .but/objects/pack/*.keep &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 2 idx-list &&
		but multi-pack-index repack --batch-size=0 &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 3 idx-list &&
		test-tool read-midx .but/objects | grep idx >midx-list &&
		test_line_count = 3 midx-list &&
		but multi-pack-index expire &&
		ls -al .but/objects/pack/*idx >idx-list &&
		test_line_count = 1 idx-list &&
		but multi-pack-index repack --batch-size=0 &&
		ls -al .but/objects/pack/*idx >new-idx-list &&
		test_cmp idx-list new-idx-list
	)
'

test_expect_success 'repack --batch-size=<large> repacks everything' '
	(
		cd dup2 &&
		rm .but/objects/pack/*.keep &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 2 idx-list &&
		but multi-pack-index repack --batch-size=2000000 &&
		ls .but/objects/pack/*idx >idx-list &&
		test_line_count = 3 idx-list &&
		test-tool read-midx .but/objects | grep idx >midx-list &&
		test_line_count = 3 midx-list &&
		but multi-pack-index expire &&
		ls -al .but/objects/pack/*idx >idx-list &&
		test_line_count = 1 idx-list
	)
'

test_expect_success 'load reverse index when missing .idx, .pack' '
	but init repo &&
	test_when_finished "rm -fr repo" &&
	(
		cd repo &&

		but config core.multiPackIndex true &&

		test_cummit base &&
		but repack -ad &&
		but multi-pack-index write &&

		but rev-parse HEAD >tip &&
		pack=$(ls .but/objects/pack/pack-*.pack) &&
		idx=$(ls .but/objects/pack/pack-*.idx) &&

		mv $idx $idx.bak &&
		but cat-file --batch-check="%(objectsize:disk)" <tip &&

		mv $idx.bak $idx &&

		mv $pack $pack.bak &&
		but cat-file --batch-check="%(objectsize:disk)" <tip
	)
'

test_expect_success 'usage shown without sub-command' '
	test_expect_code 129 but multi-pack-index 2>err &&
	! test_i18ngrep "unrecognized subcommand" err
'

test_expect_success 'complains when run outside of a repository' '
	nonbut test_must_fail but multi-pack-index write 2>err &&
	grep "not a but repository" err
'

test_done
