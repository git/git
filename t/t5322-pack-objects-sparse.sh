#!/bin/sh

test_description='pack-objects object selection using sparse algorithm'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup repo' '
	test_commit initial &&
	for i in $(test_seq 1 3)
	do
		mkdir f$i &&
		for j in $(test_seq 1 3)
		do
			mkdir f$i/f$j &&
			echo $j >f$i/f$j/data.txt || return 1
		done
	done &&
	git add . &&
	git commit -m "Initialized trees" &&
	for i in $(test_seq 1 3)
	do
		git checkout -b topic$i main &&
		echo change-$i >f$i/f$i/data.txt &&
		git commit -a -m "Changed f$i/f$i/data.txt" || return 1
	done &&
	cat >packinput.txt <<-EOF &&
	topic1
	^topic2
	^topic3
	EOF
	git rev-parse			\
		topic1			\
		topic1^{tree}		\
		topic1:f1		\
		topic1:f1/f1		\
		topic1:f1/f1/data.txt | sort >expect_objects.txt
'

test_expect_success 'non-sparse pack-objects' '
	git pack-objects --stdout --revs --no-sparse <packinput.txt >nonsparse.pack &&
	git index-pack -o nonsparse.idx nonsparse.pack &&
	git show-index <nonsparse.idx | awk "{print \$2}" >nonsparse_objects.txt &&
	test_cmp expect_objects.txt nonsparse_objects.txt
'

test_expect_success 'sparse pack-objects' '
	git pack-objects --stdout --revs --sparse <packinput.txt >sparse.pack &&
	git index-pack -o sparse.idx sparse.pack &&
	git show-index <sparse.idx | awk "{print \$2}" >sparse_objects.txt &&
	test_cmp expect_objects.txt sparse_objects.txt
'

test_expect_success 'duplicate a folder from f3 and commit to topic1' '
	git checkout topic1 &&
	echo change-3 >f3/f3/data.txt &&
	git commit -a -m "Changed f3/f3/data.txt" &&
	git rev-parse			\
		topic1~1		\
		topic1~1^{tree}		\
		topic1^{tree}		\
		topic1			\
		topic1:f1		\
		topic1:f1/f1		\
		topic1:f1/f1/data.txt | sort >required_objects.txt
'

test_expect_success 'non-sparse pack-objects' '
	git pack-objects --stdout --revs --no-sparse <packinput.txt >nonsparse.pack &&
	git index-pack -o nonsparse.idx nonsparse.pack &&
	git show-index <nonsparse.idx | awk "{print \$2}" >nonsparse_objects.txt &&
	comm -1 -2 required_objects.txt nonsparse_objects.txt >nonsparse_required_objects.txt &&
	test_cmp required_objects.txt nonsparse_required_objects.txt
'

test_expect_success 'sparse pack-objects' '
	git pack-objects --stdout --revs --sparse <packinput.txt >sparse.pack &&
	git index-pack -o sparse.idx sparse.pack &&
	git show-index <sparse.idx | awk "{print \$2}" >sparse_objects.txt &&
	comm -1 -2 required_objects.txt sparse_objects.txt >sparse_required_objects.txt &&
	test_cmp required_objects.txt sparse_required_objects.txt
'

# Demonstrate that the algorithms differ when we copy a tree wholesale
# from one folder to another.

test_expect_success 'duplicate a folder from f1 into f3' '
	mkdir f3/f4 &&
	cp -r f1/f1/* f3/f4 &&
	git add f3/f4 &&
	git commit -m "Copied f1/f1 to f3/f4" &&
	cat >packinput.txt <<-EOF &&
	topic1
	^topic1~1
	EOF
	git rev-parse		\
		topic1		\
		topic1^{tree}   \
		topic1:f3 | sort >required_objects.txt
'

test_expect_success 'non-sparse pack-objects' '
	git pack-objects --stdout --revs --no-sparse <packinput.txt >nonsparse.pack &&
	git index-pack -o nonsparse.idx nonsparse.pack &&
	git show-index <nonsparse.idx | awk "{print \$2}" >nonsparse_objects.txt &&
	comm -1 -2 required_objects.txt nonsparse_objects.txt >nonsparse_required_objects.txt &&
	test_cmp required_objects.txt nonsparse_required_objects.txt
'

# --sparse is enabled by default by pack.useSparse
test_expect_success 'sparse pack-objects' '
	GIT_TEST_PACK_SPARSE=-1 &&
	git rev-parse			\
		topic1			\
		topic1^{tree}		\
		topic1:f3		\
		topic1:f3/f4		\
		topic1:f3/f4/data.txt | sort >expect_sparse_objects.txt &&
	git pack-objects --stdout --revs <packinput.txt >sparse.pack &&
	git index-pack -o sparse.idx sparse.pack &&
	git show-index <sparse.idx | awk "{print \$2}" >sparse_objects.txt &&
	test_cmp expect_sparse_objects.txt sparse_objects.txt
'

test_expect_success 'pack.useSparse enables algorithm' '
	git config pack.useSparse true &&
	git pack-objects --stdout --revs <packinput.txt >sparse.pack &&
	git index-pack -o sparse.idx sparse.pack &&
	git show-index <sparse.idx | awk "{print \$2}" >sparse_objects.txt &&
	test_cmp expect_sparse_objects.txt sparse_objects.txt
'

test_expect_success 'pack.useSparse overridden' '
	git pack-objects --stdout --revs --no-sparse <packinput.txt >sparse.pack &&
	git index-pack -o sparse.idx sparse.pack &&
	git show-index <sparse.idx | awk "{print \$2}" >sparse_objects.txt &&
	test_cmp required_objects.txt sparse_objects.txt
'

test_done
