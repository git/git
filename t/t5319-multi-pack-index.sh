#!/bin/sh

test_description='multi-pack-indexes'
. ./test-lib.sh

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

test_expect_success 'write midx with no packs' '
	test_when_finished rm -f pack/multi-pack-index &&
	git multi-pack-index --object-dir=. write &&
	midx_read_expect 0 0 4 .
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
	pack=$(git pack-objects --index-version=1 pack/test <obj-list) &&
	test_when_finished rm pack/test-$pack.pack pack/test-$pack.idx pack/multi-pack-index &&
	git multi-pack-index --object-dir=. write &&
	midx_read_expect 1 18 4 .
'

test_expect_success 'write midx with one v2 pack' '
	git pack-objects --index-version=2,0x40 pack/test <obj-list &&
	git multi-pack-index --object-dir=. write &&
	midx_read_expect 1 18 4 .
'

test_expect_success 'add more objects' '
	for i in $(test_seq 6 10)
	do
		generate_objects $i
	done &&
	commit_and_list_objects
'

test_expect_success 'write midx with two packs' '
	git pack-objects --index-version=1 pack/test-2 <obj-list &&
	git multi-pack-index --object-dir=. write &&
	midx_read_expect 2 34 4 .
'

test_expect_success 'add more packs' '
	for j in $(test_seq 11 20)
	do
		generate_objects $j &&
		commit_and_list_objects &&
		git pack-objects --index-version=2 pack/test-pack <obj-list
	done
'

test_expect_success 'write midx with twelve packs' '
	git multi-pack-index --object-dir=. write &&
	midx_read_expect 12 74 4 .
'

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
	corrupt_data $idx64 2999 "\02" &&
	midx64=$(git multi-pack-index --object-dir=objects64 write) &&
	midx_read_expect 1 63 5 objects64 " large-offsets"
'

test_done
