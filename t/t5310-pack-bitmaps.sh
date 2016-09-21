#!/bin/sh

test_description='exercise basic bitmap functionality'
. ./test-lib.sh

objpath () {
	echo ".git/objects/$(echo "$1" | sed -e 's|\(..\)|\1/|')"
}

# show objects present in pack ($1 should be associated *.idx)
list_packed_objects () {
	git show-index <"$1" | cut -d' ' -f2
}

# has_any pattern-file content-file
# tests whether content-file has any entry from pattern-file with entries being
# whole lines.
has_any () {
	grep -Ff "$1" "$2"
}

test_expect_success 'setup repo with moderate-sized history' '
	for i in $(test_seq 1 10); do
		test_commit $i
	done &&
	git checkout -b other HEAD~5 &&
	for i in $(test_seq 1 10); do
		test_commit side-$i
	done &&
	git checkout master &&
	bitmaptip=$(git rev-parse master) &&
	blob=$(echo tagged-blob | git hash-object -w --stdin) &&
	git tag tagged-blob $blob &&
	git config repack.writebitmaps true &&
	git config pack.writebitmaphashcache true
'

test_expect_success 'full repack creates bitmaps' '
	git repack -ad &&
	ls .git/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output
'

test_expect_success 'rev-list --test-bitmap verifies bitmaps' '
	git rev-list --test-bitmap HEAD
'

rev_list_tests() {
	state=$1

	test_expect_success "counting commits via bitmap ($state)" '
		git rev-list --count HEAD >expect &&
		git rev-list --use-bitmap-index --count HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting partial commits via bitmap ($state)" '
		git rev-list --count HEAD~5..HEAD >expect &&
		git rev-list --use-bitmap-index --count HEAD~5..HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting commits with limit ($state)" '
		git rev-list --count -n 1 HEAD >expect &&
		git rev-list --use-bitmap-index --count -n 1 HEAD >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting non-linear history ($state)" '
		git rev-list --count other...master >expect &&
		git rev-list --use-bitmap-index --count other...master >actual &&
		test_cmp expect actual
	'

	test_expect_success "counting commits with limiting ($state)" '
		git rev-list --count HEAD -- 1.t >expect &&
		git rev-list --use-bitmap-index --count HEAD -- 1.t >actual &&
		test_cmp expect actual
	'

	test_expect_success "enumerate --objects ($state)" '
		git rev-list --objects --use-bitmap-index HEAD >tmp &&
		cut -d" " -f1 <tmp >tmp2 &&
		sort <tmp2 >actual &&
		git rev-list --objects HEAD >tmp &&
		cut -d" " -f1 <tmp >tmp2 &&
		sort <tmp2 >expect &&
		test_cmp expect actual
	'

	test_expect_success "bitmap --objects handles non-commit objects ($state)" '
		git rev-list --objects --use-bitmap-index HEAD tagged-blob >actual &&
		grep $blob actual
	'
}

rev_list_tests 'full bitmap'

test_expect_success 'clone from bitmapped repository' '
	git clone --no-local --bare . clone.git &&
	git rev-parse HEAD >expect &&
	git --git-dir=clone.git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'setup further non-bitmapped commits' '
	for i in $(test_seq 1 10); do
		test_commit further-$i
	done
'

rev_list_tests 'partial bitmap'

test_expect_success 'fetch (partial bitmap)' '
	git --git-dir=clone.git fetch origin master:master &&
	git rev-parse HEAD >expect &&
	git --git-dir=clone.git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'incremental repack cannot create bitmaps' '
	test_commit more-1 &&
	find .git/objects/pack -name "*.bitmap" >expect &&
	git repack -d &&
	find .git/objects/pack -name "*.bitmap" >actual &&
	test_cmp expect actual
'

test_expect_success 'incremental repack can disable bitmaps' '
	test_commit more-2 &&
	git repack -d --no-write-bitmap-index
'

test_expect_success 'pack-objects respects --local (non-local loose)' '
	git init --bare alt.git &&
	echo $(pwd)/alt.git/objects >.git/objects/info/alternates &&
	echo content1 >file1 &&
	# non-local loose object which is not present in bitmapped pack
	altblob=$(GIT_DIR=alt.git git hash-object -w file1) &&
	# non-local loose object which is also present in bitmapped pack
	git cat-file blob $blob | GIT_DIR=alt.git git hash-object -w --stdin &&
	git add file1 &&
	test_tick &&
	git commit -m commit_file1 &&
	echo HEAD | git pack-objects --local --stdout --revs >1.pack &&
	git index-pack 1.pack &&
	list_packed_objects 1.idx >1.objects &&
	printf "%s\n" "$altblob" "$blob" >nonlocal-loose &&
	! has_any nonlocal-loose 1.objects
'

test_expect_success 'pack-objects respects --honor-pack-keep (local non-bitmapped pack)' '
	echo content2 >file2 &&
	blob2=$(git hash-object -w file2) &&
	git add file2 &&
	test_tick &&
	git commit -m commit_file2 &&
	printf "%s\n" "$blob2" "$bitmaptip" >keepobjects &&
	pack2=$(git pack-objects pack2 <keepobjects) &&
	mv pack2-$pack2.* .git/objects/pack/ &&
	>.git/objects/pack/pack2-$pack2.keep &&
	rm $(objpath $blob2) &&
	echo HEAD | git pack-objects --honor-pack-keep --stdout --revs >2a.pack &&
	git index-pack 2a.pack &&
	list_packed_objects 2a.idx >2a.objects &&
	! has_any keepobjects 2a.objects
'

test_expect_success 'pack-objects respects --local (non-local pack)' '
	mv .git/objects/pack/pack2-$pack2.* alt.git/objects/pack/ &&
	echo HEAD | git pack-objects --local --stdout --revs >2b.pack &&
	git index-pack 2b.pack &&
	list_packed_objects 2b.idx >2b.objects &&
	! has_any keepobjects 2b.objects
'

test_expect_success 'pack-objects respects --honor-pack-keep (local bitmapped pack)' '
	ls .git/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output &&
	packbitmap=$(basename $(cat output) .bitmap) &&
	list_packed_objects .git/objects/pack/$packbitmap.idx >packbitmap.objects &&
	test_when_finished "rm -f .git/objects/pack/$packbitmap.keep" &&
	>.git/objects/pack/$packbitmap.keep &&
	echo HEAD | git pack-objects --honor-pack-keep --stdout --revs >3a.pack &&
	git index-pack 3a.pack &&
	list_packed_objects 3a.idx >3a.objects &&
	! has_any packbitmap.objects 3a.objects
'

test_expect_success 'pack-objects respects --local (non-local bitmapped pack)' '
	mv .git/objects/pack/$packbitmap.* alt.git/objects/pack/ &&
	test_when_finished "mv alt.git/objects/pack/$packbitmap.* .git/objects/pack/" &&
	echo HEAD | git pack-objects --local --stdout --revs >3b.pack &&
	git index-pack 3b.pack &&
	list_packed_objects 3b.idx >3b.objects &&
	! has_any packbitmap.objects 3b.objects
'

test_expect_success 'pack-objects to file can use bitmap' '
	# make sure we still have 1 bitmap index from previous tests
	ls .git/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output &&
	# verify equivalent packs are generated with/without using bitmap index
	packasha1=$(git pack-objects --no-use-bitmap-index --all packa </dev/null) &&
	packbsha1=$(git pack-objects --use-bitmap-index --all packb </dev/null) &&
	list_packed_objects <packa-$packasha1.idx >packa.objects &&
	list_packed_objects <packb-$packbsha1.idx >packb.objects &&
	test_cmp packa.objects packb.objects
'

test_expect_success 'full repack, reusing previous bitmaps' '
	git repack -ad &&
	ls .git/objects/pack/ | grep bitmap >output &&
	test_line_count = 1 output
'

test_expect_success 'fetch (full bitmap)' '
	git --git-dir=clone.git fetch origin master:master &&
	git rev-parse HEAD >expect &&
	git --git-dir=clone.git rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'create objects for missing-HAVE tests' '
	blob=$(echo "missing have" | git hash-object -w --stdin) &&
	tree=$(printf "100644 blob $blob\tfile\n" | git mktree) &&
	parent=$(echo parent | git commit-tree $tree) &&
	commit=$(echo commit | git commit-tree $tree -p $parent) &&
	cat >revs <<-EOF
	HEAD
	^HEAD^
	^$commit
	EOF
'

test_expect_success 'pack-objects respects --incremental' '
	cat >revs2 <<-EOF &&
	HEAD
	$commit
	EOF
	git pack-objects --incremental --stdout --revs <revs2 >4.pack &&
	git index-pack 4.pack &&
	list_packed_objects 4.idx >4.objects &&
	test_line_count = 4 4.objects &&
	git rev-list --objects $commit >revlist &&
	cut -d" " -f1 revlist |sort >objects &&
	test_cmp 4.objects objects
'

test_expect_success 'pack with missing blob' '
	rm $(objpath $blob) &&
	git pack-objects --stdout --revs <revs >/dev/null
'

test_expect_success 'pack with missing tree' '
	rm $(objpath $tree) &&
	git pack-objects --stdout --revs <revs >/dev/null
'

test_expect_success 'pack with missing parent' '
	rm $(objpath $parent) &&
	git pack-objects --stdout --revs <revs >/dev/null
'

test_expect_success JGIT 'we can read jgit bitmaps' '
	git clone . compat-jgit &&
	(
		cd compat-jgit &&
		rm -f .git/objects/pack/*.bitmap &&
		jgit gc &&
		git rev-list --test-bitmap HEAD
	)
'

test_expect_success JGIT 'jgit can read our bitmaps' '
	git clone . compat-us &&
	(
		cd compat-us &&
		git repack -adb &&
		# jgit gc will barf if it does not like our bitmaps
		jgit gc
	)
'

test_expect_success 'splitting packs does not generate bogus bitmaps' '
	test-genrandom foo $((1024 * 1024)) >rand &&
	git add rand &&
	git commit -m "commit with big file" &&
	git -c pack.packSizeLimit=500k repack -adb &&
	git init --bare no-bitmaps.git &&
	git -C no-bitmaps.git fetch .. HEAD
'

test_done
