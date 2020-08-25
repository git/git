#!/bin/sh

test_description='multi-pack-indexes'
. ./test-lib.sh

GIT_TEST_MULTI_PACK_INDEX=0
objdir=.git/objects

midx_read_expect () {
	NUM_PACKS=$1
	NUM_OBJECTS=$2
	NUM_CHUNKS=$3
	OBJECT_DIR=$4
	EXTRA_CHUNKS="$5"
	{
		cat <<-EOF &&
		header: 4d494458 1 $NUM_CHUNKS $NUM_PACKS
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
	test_must_fail git multi-pack-index --object-dir=. write &&
	test_path_is_missing pack/multi-pack-index
'

test_expect_success "Warn if a midx contains no oid" '
	cp "$TEST_DIRECTORY"/t5319/no-objects.midx $objdir/pack/multi-pack-index &&
	test_must_fail git multi-pack-index verify &&
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
	git update-index --add file_$iii deep_delta_$iii wide_delta_$iii
}

commit_and_list_objects () {
	{
		echo 101 &&
		test-tool genrandom 100 8192;
	} >file_101 &&
	git update-index --add file_101 &&
	tree=$(git write-tree) &&
	commit=$(git commit-tree $tree -p HEAD</dev/null) &&
	{
		echo $tree &&
		git ls-tree $tree | sed -e "s/.* \\([0-9a-f]*\\)	.*/\\1/"
	} >obj-list &&
	git reset --hard $commit
}

test_expect_success 'create objects' '
	test_commit initial &&
	for i in $(test_seq 1 5)
	do
		generate_objects $i
	done &&
	commit_and_list_objects
'

test_expect_success 'write midx with one v1 pack' '
	pack=$(git pack-objects --index-version=1 $objdir/pack/test <obj-list) &&
	test_when_finished rm $objdir/pack/test-$pack.pack \
		$objdir/pack/test-$pack.idx $objdir/pack/multi-pack-index &&
	git multi-pack-index --object-dir=$objdir write &&
	midx_read_expect 1 18 4 $objdir
'

midx_git_two_modes () {
	git -c core.multiPackIndex=false $1 >expect &&
	git -c core.multiPackIndex=true $1 >actual &&
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
	test_expect_success "check normal git operations: $MSG" '
		midx_git_two_modes "rev-list --objects --all" &&
		midx_git_two_modes "log --raw" &&
		midx_git_two_modes "count-objects --verbose" &&
		midx_git_two_modes "cat-file --batch-all-objects --batch-check" &&
		midx_git_two_modes "cat-file --batch-all-objects --batch-check --unordered" sorted
	'
}

test_expect_success 'write midx with one v2 pack' '
	git pack-objects --index-version=2,0x40 $objdir/pack/test <obj-list &&
	git multi-pack-index --object-dir=$objdir write &&
	midx_read_expect 1 18 4 $objdir
'

compare_results_with_midx "one v2 pack"

test_expect_success 'corrupt idx not opened' '
	idx=$(test-tool read-midx $objdir | grep "\.idx\$") &&
	mv $objdir/pack/$idx backup-$idx &&
	test_when_finished "mv backup-\$idx \$objdir/pack/\$idx" &&

	# This is the minimum size for a sha-1 based .idx; this lets
	# us pass perfunctory tests, but anything that actually opens and reads
	# the idx file will complain.
	test_copy_bytes 1064 <backup-$idx >$objdir/pack/$idx &&

	git -c core.multiPackIndex=true rev-list --objects --all 2>err &&
	test_must_be_empty err
'

test_expect_success 'add more objects' '
	for i in $(test_seq 6 10)
	do
		generate_objects $i
	done &&
	commit_and_list_objects
'

test_expect_success 'write midx with two packs' '
	git pack-objects --index-version=1 $objdir/pack/test-2 <obj-list &&
	git multi-pack-index --object-dir=$objdir write &&
	midx_read_expect 2 34 4 $objdir
'

compare_results_with_midx "two packs"

test_expect_success 'write progress off for redirected stderr' '
	git multi-pack-index --object-dir=$objdir write 2>err &&
	test_line_count = 0 err
'

test_expect_success 'write force progress on for stderr' '
	GIT_PROGRESS_DELAY=0 git multi-pack-index --object-dir=$objdir --progress write 2>err &&
	test_file_not_empty err
'

test_expect_success 'write with the --no-progress option' '
	GIT_PROGRESS_DELAY=0 git multi-pack-index --object-dir=$objdir --no-progress write 2>err &&
	test_line_count = 0 err
'

test_expect_success 'add more packs' '
	for j in $(test_seq 11 20)
	do
		generate_objects $j &&
		commit_and_list_objects &&
		git pack-objects --index-version=2 $objdir/pack/test-pack <obj-list
	done
'

compare_results_with_midx "mixed mode (two packs + extra)"

test_expect_success 'write midx with twelve packs' '
	git multi-pack-index --object-dir=$objdir write &&
	midx_read_expect 12 74 4 $objdir
'

compare_results_with_midx "twelve packs"

test_expect_success 'verify multi-pack-index success' '
	git multi-pack-index verify --object-dir=$objdir
'

test_expect_success 'verify progress off for redirected stderr' '
	git multi-pack-index verify --object-dir=$objdir 2>err &&
	test_line_count = 0 err
'

test_expect_success 'verify force progress on for stderr' '
	git multi-pack-index verify --object-dir=$objdir --progress 2>err &&
	test_file_not_empty err
'

test_expect_success 'verify with the --no-progress option' '
	git multi-pack-index verify --object-dir=$objdir --no-progress 2>err &&
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
		COMMAND="git multi-pack-index verify --object-dir=$OBJDIR"
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

HASH_LEN=$(test_oid rawsz)
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
	corrupt_midx_and_verify $MIDX_BYTE_OID_VERSION "\02" $objdir \
		"hash version"
'

test_expect_success 'verify truncated chunk count' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_COUNT "\01" $objdir \
		"missing required"
'

test_expect_success 'verify extended chunk count' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_COUNT "\07" $objdir \
		"terminating multi-pack-index chunk id appears earlier than expected"
'

test_expect_success 'verify missing required chunk' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_ID "\01" $objdir \
		"missing required"
'

test_expect_success 'verify invalid chunk offset' '
	corrupt_midx_and_verify $MIDX_BYTE_CHUNK_OFFSET "\01" $objdir \
		"invalid chunk offset (too large)"
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

test_expect_success 'git-fsck incorrect offset' '
	corrupt_midx_and_verify $MIDX_BYTE_OFFSET "\377" $objdir \
		"incorrect object offset" \
		"git -c core.multipackindex=true fsck"
'

test_expect_success 'repack progress off for redirected stderr' '
	GIT_PROGRESS_DELAY=0 git multi-pack-index --object-dir=$objdir repack 2>err &&
	test_line_count = 0 err
'

test_expect_success 'repack force progress on for stderr' '
	GIT_PROGRESS_DELAY=0 git multi-pack-index --object-dir=$objdir --progress repack 2>err &&
	test_file_not_empty err
'

test_expect_success 'repack with the --no-progress option' '
	GIT_PROGRESS_DELAY=0 git multi-pack-index --object-dir=$objdir --no-progress repack 2>err &&
	test_line_count = 0 err
'

test_expect_success 'repack removes multi-pack-index' '
	test_path_is_file $objdir/pack/multi-pack-index &&
	GIT_TEST_MULTI_PACK_INDEX=0 git repack -adf &&
	test_path_is_missing $objdir/pack/multi-pack-index
'

compare_results_with_midx "after repack"

test_expect_success 'multi-pack-index and pack-bitmap' '
	git -c repack.writeBitmaps=true repack -ad &&
	git multi-pack-index write &&
	git rev-list --test-bitmap HEAD
'

test_expect_success 'multi-pack-index and alternates' '
	git init --bare alt.git &&
	echo $(pwd)/alt.git/objects >.git/objects/info/alternates &&
	echo content1 >file1 &&
	altblob=$(GIT_DIR=alt.git git hash-object -w file1) &&
	git cat-file blob $altblob &&
	git rev-list --all
'

compare_results_with_midx "with alternate (local midx)"

test_expect_success 'multi-pack-index in an alternate' '
	mv .git/objects/pack/* alt.git/objects/pack &&
	test_commit add_local_objects &&
	git repack --local &&
	git multi-pack-index write &&
	midx_read_expect 1 3 4 $objdir &&
	git reset --hard HEAD~1 &&
	rm -f .git/objects/pack/*
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
		generate_objects 11
	done &&
	commit_and_list_objects &&
	pack64=$(git pack-objects --index-version=2,0x40 objects64/pack/test-64 <obj-list) &&
	idx64=objects64/pack/test-64-$pack64.idx &&
	chmod u+w $idx64 &&
	corrupt_data $idx64 $(test_oid idxoff) "\02" &&
	midx64=$(git multi-pack-index --object-dir=objects64 write) &&
	midx_read_expect 1 63 5 objects64 " large-offsets"
'

test_expect_success 'verify multi-pack-index with 64-bit offsets' '
	git multi-pack-index verify --object-dir=objects64
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
		git init &&
		test-tool genrandom "data" 4096 >large_file.txt &&
		git update-index --add large_file.txt &&
		for i in $(test_seq 1 20)
		do
			test_commit $i
		done &&
		git branch A HEAD &&
		git branch B HEAD~8 &&
		git branch C HEAD~13 &&
		git branch D HEAD~16 &&
		git branch E HEAD~18 &&
		git pack-objects --revs .git/objects/pack/pack-A <<-EOF &&
		refs/heads/A
		^refs/heads/B
		EOF
		git pack-objects --revs .git/objects/pack/pack-B <<-EOF &&
		refs/heads/B
		^refs/heads/C
		EOF
		git pack-objects --revs .git/objects/pack/pack-C <<-EOF &&
		refs/heads/C
		^refs/heads/D
		EOF
		git pack-objects --revs .git/objects/pack/pack-D <<-EOF &&
		refs/heads/D
		^refs/heads/E
		EOF
		git pack-objects --revs .git/objects/pack/pack-E <<-EOF &&
		refs/heads/E
		EOF
		git multi-pack-index write &&
		cp -r .git/objects/pack .git/objects/pack-backup
	)
'

test_expect_success 'expire does not remove any packs' '
	(
		cd dup &&
		ls .git/objects/pack >expect &&
		git multi-pack-index expire &&
		ls .git/objects/pack >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'expire progress off for redirected stderr' '
	(
		cd dup &&
		git multi-pack-index expire 2>err &&
		test_line_count = 0 err
	)
'

test_expect_success 'expire force progress on for stderr' '
	(
		cd dup &&
		GIT_PROGRESS_DELAY=0 git multi-pack-index --progress expire 2>err &&
		test_file_not_empty err
	)
'

test_expect_success 'expire with the --no-progress option' '
	(
		cd dup &&
		GIT_PROGRESS_DELAY=0 git multi-pack-index --no-progress expire 2>err &&
		test_line_count = 0 err
	)
'

test_expect_success 'expire removes unreferenced packs' '
	(
		cd dup &&
		git pack-objects --revs .git/objects/pack/pack-combined <<-EOF &&
		refs/heads/A
		^refs/heads/C
		EOF
		git multi-pack-index write &&
		ls .git/objects/pack | grep -v -e pack-[AB] >expect &&
		git multi-pack-index expire &&
		ls .git/objects/pack >actual &&
		test_cmp expect actual &&
		ls .git/objects/pack/ | grep idx >expect-idx &&
		test-tool read-midx .git/objects | grep idx >actual-midx &&
		test_cmp expect-idx actual-midx &&
		git multi-pack-index verify &&
		git fsck
	)
'

test_expect_success 'repack with minimum size does not alter existing packs' '
	(
		cd dup &&
		rm -rf .git/objects/pack &&
		mv .git/objects/pack-backup .git/objects/pack &&
		test-tool chmtime =-5 .git/objects/pack/pack-D* &&
		test-tool chmtime =-4 .git/objects/pack/pack-C* &&
		test-tool chmtime =-3 .git/objects/pack/pack-B* &&
		test-tool chmtime =-2 .git/objects/pack/pack-A* &&
		ls .git/objects/pack >expect &&
		MINSIZE=$(test-tool path-utils file-size .git/objects/pack/*pack | sort -n | head -n 1) &&
		git multi-pack-index repack --batch-size=$MINSIZE &&
		ls .git/objects/pack >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'repack respects repack.packKeptObjects=false' '
	test_when_finished rm -f dup/.git/objects/pack/*keep &&
	(
		cd dup &&
		ls .git/objects/pack/*idx >idx-list &&
		test_line_count = 5 idx-list &&
		ls .git/objects/pack/*.pack | sed "s/\.pack/.keep/" >keep-list &&
		test_line_count = 5 keep-list &&
		for keep in $(cat keep-list)
		do
			touch $keep || return 1
		done &&
		git multi-pack-index repack --batch-size=0 &&
		ls .git/objects/pack/*idx >idx-list &&
		test_line_count = 5 idx-list &&
		test-tool read-midx .git/objects | grep idx >midx-list &&
		test_line_count = 5 midx-list &&
		THIRD_SMALLEST_SIZE=$(test-tool path-utils file-size .git/objects/pack/*pack | sort -n | sed -n 3p) &&
		BATCH_SIZE=$((THIRD_SMALLEST_SIZE + 1)) &&
		git multi-pack-index repack --batch-size=$BATCH_SIZE &&
		ls .git/objects/pack/*idx >idx-list &&
		test_line_count = 5 idx-list &&
		test-tool read-midx .git/objects | grep idx >midx-list &&
		test_line_count = 5 midx-list
	)
'

test_expect_success 'repack creates a new pack' '
	(
		cd dup &&
		ls .git/objects/pack/*idx >idx-list &&
		test_line_count = 5 idx-list &&
		THIRD_SMALLEST_SIZE=$(test-tool path-utils file-size .git/objects/pack/*pack | sort -n | head -n 3 | tail -n 1) &&
		BATCH_SIZE=$(($THIRD_SMALLEST_SIZE + 1)) &&
		git multi-pack-index repack --batch-size=$BATCH_SIZE &&
		ls .git/objects/pack/*idx >idx-list &&
		test_line_count = 6 idx-list &&
		test-tool read-midx .git/objects | grep idx >midx-list &&
		test_line_count = 6 midx-list
	)
'

test_expect_success 'expire removes repacked packs' '
	(
		cd dup &&
		ls -al .git/objects/pack/*pack &&
		ls -S .git/objects/pack/*pack | head -n 4 >expect &&
		git multi-pack-index expire &&
		ls -S .git/objects/pack/*pack >actual &&
		test_cmp expect actual &&
		test-tool read-midx .git/objects | grep idx >midx-list &&
		test_line_count = 4 midx-list
	)
'

test_expect_success 'expire works when adding new packs' '
	(
		cd dup &&
		git pack-objects --revs .git/objects/pack/pack-combined <<-EOF &&
		refs/heads/A
		^refs/heads/B
		EOF
		git pack-objects --revs .git/objects/pack/pack-combined <<-EOF &&
		refs/heads/B
		^refs/heads/C
		EOF
		git pack-objects --revs .git/objects/pack/pack-combined <<-EOF &&
		refs/heads/C
		^refs/heads/D
		EOF
		git multi-pack-index write &&
		git pack-objects --revs .git/objects/pack/a-pack <<-EOF &&
		refs/heads/D
		^refs/heads/E
		EOF
		git multi-pack-index write &&
		git pack-objects --revs .git/objects/pack/z-pack <<-EOF &&
		refs/heads/E
		EOF
		git multi-pack-index expire &&
		ls .git/objects/pack/ | grep idx >expect &&
		test-tool read-midx .git/objects | grep idx >actual &&
		test_cmp expect actual &&
		git multi-pack-index verify
	)
'

test_expect_success 'expire respects .keep files' '
	(
		cd dup &&
		git pack-objects --revs .git/objects/pack/pack-all <<-EOF &&
		refs/heads/A
		EOF
		git multi-pack-index write &&
		PACKA=$(ls .git/objects/pack/a-pack*\.pack | sed s/\.pack\$//) &&
		touch $PACKA.keep &&
		git multi-pack-index expire &&
		ls -S .git/objects/pack/a-pack* | grep $PACKA >a-pack-files &&
		test_line_count = 3 a-pack-files &&
		test-tool read-midx .git/objects | grep idx >midx-list &&
		test_line_count = 2 midx-list
	)
'

test_expect_success 'repack --batch-size=0 repacks everything' '
	(
		cd dup &&
		rm .git/objects/pack/*.keep &&
		ls .git/objects/pack/*idx >idx-list &&
		test_line_count = 2 idx-list &&
		git multi-pack-index repack --batch-size=0 &&
		ls .git/objects/pack/*idx >idx-list &&
		test_line_count = 3 idx-list &&
		test-tool read-midx .git/objects | grep idx >midx-list &&
		test_line_count = 3 midx-list &&
		git multi-pack-index expire &&
		ls -al .git/objects/pack/*idx >idx-list &&
		test_line_count = 1 idx-list &&
		git multi-pack-index repack --batch-size=0 &&
		ls -al .git/objects/pack/*idx >new-idx-list &&
		test_cmp idx-list new-idx-list
	)
'

test_done
