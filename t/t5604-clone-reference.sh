#!/bin/sh
#
# Copyright (C) 2006 Martin Waitz <tali@admingilde.org>
#

test_description='test clone --reference'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

base_dir=$(pwd)

U=$base_dir/UPLOAD_LOG

# create a cummit in repo $1 with name $2
cummit_in () {
	(
		cd "$1" &&
		echo "$2" >"$2" &&
		but add "$2" &&
		but cummit -m "$2"
	)
}

# check that there are $2 loose objects in repo $1
test_objcount () {
	echo "$2" >expect &&
	but -C "$1" count-objects >actual.raw &&
	cut -d' ' -f1 <actual.raw >actual &&
	test_cmp expect actual
}

test_expect_success 'preparing first repository' '
	test_create_repo A &&
	cummit_in A file1
'

test_expect_success 'preparing second repository' '
	but clone A B &&
	cummit_in B file2 &&
	but -C B repack -ad &&
	but -C B prune
'

test_expect_success 'cloning with reference (-l -s)' '
	but clone -l -s --reference B A C
'

test_expect_success 'existence of info/alternates' '
	test_line_count = 2 C/.but/objects/info/alternates
'

test_expect_success 'pulling from reference' '
	but -C C pull ../B main
'

test_expect_success 'that reference gets used' '
	test_objcount C 0
'

test_expect_success 'cloning with reference (no -l -s)' '
	GIT_TRACE_PACKET=$U.D but clone --reference B "file://$(pwd)/A" D
'

test_expect_success 'fetched no objects' '
	test -s "$U.D" &&
	! grep " want" "$U.D"
'

test_expect_success 'existence of info/alternates' '
	test_line_count = 1 D/.but/objects/info/alternates
'

test_expect_success 'pulling from reference' '
	but -C D pull ../B main
'

test_expect_success 'that reference gets used' '
	test_objcount D 0
'

test_expect_success 'updating origin' '
	cummit_in A file3 &&
	but -C A repack -ad &&
	but -C A prune
'

test_expect_success 'pulling changes from origin' '
	but -C C pull --no-rebase origin
'

# the 2 local objects are cummit and tree from the merge
test_expect_success 'that alternate to origin gets used' '
	test_objcount C 2
'

test_expect_success 'pulling changes from origin' '
	but -C D pull --no-rebase origin
'

# the 5 local objects are expected; file3 blob, cummit in A to add it
# and its tree, and 2 are our tree and the merge cummit.
test_expect_success 'check objects expected to exist locally' '
	test_objcount D 5
'

test_expect_success 'preparing alternate repository #1' '
	test_create_repo F &&
	cummit_in F file1
'

test_expect_success 'cloning alternate repo #2 and adding changes to repo #1' '
	but clone F G &&
	cummit_in F file2
'

test_expect_success 'cloning alternate repo #1, using #2 as reference' '
	but clone --reference G F H
'

test_expect_success 'cloning with reference being subset of source (-l -s)' '
	but clone -l -s --reference A B E
'

test_expect_success 'cloning with multiple references drops duplicates' '
	but clone -s --reference B --reference A --reference B A dups &&
	test_line_count = 2 dups/.but/objects/info/alternates
'

test_expect_success 'clone with reference from a tagged repository' '
	(
		cd A && but tag -a -m tagged HEAD
	) &&
	but clone --reference=A A I
'

test_expect_success 'prepare branched repository' '
	but clone A J &&
	(
		cd J &&
		but checkout -b other main^ &&
		echo other >otherfile &&
		but add otherfile &&
		but cummit -m other &&
		but checkout main
	)
'

test_expect_success 'fetch with incomplete alternates' '
	but init K &&
	echo "$base_dir/A/.but/objects" >K/.but/objects/info/alternates &&
	(
		cd K &&
		but remote add J "file://$base_dir/J" &&
		GIT_TRACE_PACKET=$U.K but fetch J
	) &&
	main_object=$(cd A && but for-each-ref --format="%(objectname)" refs/heads/main) &&
	test -s "$U.K" &&
	! grep " want $main_object" "$U.K" &&
	tag_object=$(cd A && but for-each-ref --format="%(objectname)" refs/tags/HEAD) &&
	! grep " want $tag_object" "$U.K"
'

test_expect_success 'clone using repo with butfile as a reference' '
	but clone --separate-but-dir=L A M &&
	but clone --reference=M A N &&
	echo "$base_dir/L/objects" >expected &&
	test_cmp expected "$base_dir/N/.but/objects/info/alternates"
'

test_expect_success 'clone using repo pointed at by butfile as reference' '
	but clone --reference=M/.but A O &&
	echo "$base_dir/L/objects" >expected &&
	test_cmp expected "$base_dir/O/.but/objects/info/alternates"
'

test_expect_success 'clone and dissociate from reference' '
	but init P &&
	(
		cd P && test_cummit one
	) &&
	but clone P Q &&
	(
		cd Q && test_cummit two
	) &&
	but clone --no-local --reference=P Q R &&
	but clone --no-local --reference=P --dissociate Q S &&
	# removing the reference P would corrupt R but not S
	rm -fr P &&
	test_must_fail but -C R fsck &&
	but -C S fsck
'
test_expect_success 'clone, dissociate from partial reference and repack' '
	rm -fr P Q R &&
	but init P &&
	(
		cd P &&
		test_cummit one &&
		but repack &&
		test_cummit two &&
		but repack
	) &&
	but clone --bare P Q &&
	(
		cd P &&
		but checkout -b second &&
		test_cummit three &&
		but repack
	) &&
	but clone --bare --dissociate --reference=P Q R &&
	ls R/objects/pack/*.pack >packs.txt &&
	test_line_count = 1 packs.txt
'

test_expect_success 'clone, dissociate from alternates' '
	rm -fr A B C &&
	test_create_repo A &&
	cummit_in A file1 &&
	but clone --reference=A A B &&
	test_line_count = 1 B/.but/objects/info/alternates &&
	but clone --local --dissociate B C &&
	! test -f C/.but/objects/info/alternates &&
	( cd C && but fsck )
'

test_expect_success 'setup repo with garbage in objects/*' '
	but init S &&
	(
		cd S &&
		test_cummit A &&

		cd .but/objects &&
		>.some-hidden-file &&
		>some-file &&
		mkdir .some-hidden-dir &&
		>.some-hidden-dir/some-file &&
		>.some-hidden-dir/.some-dot-file &&
		mkdir some-dir &&
		>some-dir/some-file &&
		>some-dir/.some-dot-file
	)
'

test_expect_success 'clone a repo with garbage in objects/*' '
	for option in --local --no-hardlinks --shared --dissociate
	do
		but clone $option S S$option || return 1 &&
		but -C S$option fsck || return 1
	done &&
	find S-* -name "*some*" | sort >actual &&
	cat >expected <<-EOF &&
	S--dissociate/.but/objects/.some-hidden-dir
	S--dissociate/.but/objects/.some-hidden-dir/.some-dot-file
	S--dissociate/.but/objects/.some-hidden-dir/some-file
	S--dissociate/.but/objects/.some-hidden-file
	S--dissociate/.but/objects/some-dir
	S--dissociate/.but/objects/some-dir/.some-dot-file
	S--dissociate/.but/objects/some-dir/some-file
	S--dissociate/.but/objects/some-file
	S--local/.but/objects/.some-hidden-dir
	S--local/.but/objects/.some-hidden-dir/.some-dot-file
	S--local/.but/objects/.some-hidden-dir/some-file
	S--local/.but/objects/.some-hidden-file
	S--local/.but/objects/some-dir
	S--local/.but/objects/some-dir/.some-dot-file
	S--local/.but/objects/some-dir/some-file
	S--local/.but/objects/some-file
	S--no-hardlinks/.but/objects/.some-hidden-dir
	S--no-hardlinks/.but/objects/.some-hidden-dir/.some-dot-file
	S--no-hardlinks/.but/objects/.some-hidden-dir/some-file
	S--no-hardlinks/.but/objects/.some-hidden-file
	S--no-hardlinks/.but/objects/some-dir
	S--no-hardlinks/.but/objects/some-dir/.some-dot-file
	S--no-hardlinks/.but/objects/some-dir/some-file
	S--no-hardlinks/.but/objects/some-file
	EOF
	test_cmp expected actual
'

test_expect_success SYMLINKS 'setup repo with manually symlinked or unknown files at objects/' '
	but init T &&
	(
		cd T &&
		but config gc.auto 0 &&
		test_cummit A &&
		but gc &&
		test_cummit B &&

		cd .but/objects &&
		mv pack packs &&
		ln -s packs pack &&
		find ?? -type d >loose-dirs &&
		last_loose=$(tail -n 1 loose-dirs) &&
		mv $last_loose a-loose-dir &&
		ln -s a-loose-dir $last_loose &&
		first_loose=$(head -n 1 loose-dirs) &&
		rm -f loose-dirs &&

		cd $first_loose &&
		obj=$(ls *) &&
		mv $obj ../an-object &&
		ln -s ../an-object $obj &&

		cd ../ &&
		find . -type f | sort >../../../T.objects-files.raw &&
		find . -type l | sort >../../../T.objects-symlinks.raw &&
		echo unknown_content >unknown_file
	) &&
	but -C T fsck &&
	but -C T rev-list --all --objects >T.objects
'


test_expect_success SYMLINKS 'clone repo with symlinked or unknown files at objects/' '
	for option in --local --no-hardlinks --shared --dissociate
	do
		but clone $option T T$option || return 1 &&
		but -C T$option fsck || return 1 &&
		but -C T$option rev-list --all --objects >T$option.objects &&
		test_cmp T.objects T$option.objects &&
		(
			cd T$option/.but/objects &&
			find . -type f | sort >../../../T$option.objects-files.raw &&
			find . -type l | sort >../../../T$option.objects-symlinks.raw
		)
	done &&

	for raw in $(ls T*.raw)
	do
		sed -e "s!/../!/Y/!; s![0-9a-f]\{38,\}!Z!" -e "/cummit-graph/d" \
		    -e "/multi-pack-index/d" -e "/rev/d" <$raw >$raw.de-sha-1 &&
		sort $raw.de-sha-1 >$raw.de-sha || return 1
	done &&

	cat >expected-files <<-EOF &&
	./Y/Z
	./Y/Z
	./Y/Z
	./a-loose-dir/Z
	./an-object
	./info/packs
	./pack/pack-Z.idx
	./pack/pack-Z.pack
	./packs/pack-Z.idx
	./packs/pack-Z.pack
	./unknown_file
	EOF

	for option in --local --no-hardlinks --dissociate
	do
		test_cmp expected-files T$option.objects-files.raw.de-sha || return 1 &&
		test_must_be_empty T$option.objects-symlinks.raw.de-sha || return 1
	done &&

	echo ./info/alternates >expected-files &&
	test_cmp expected-files T--shared.objects-files.raw &&
	test_must_be_empty T--shared.objects-symlinks.raw
'

test_done
