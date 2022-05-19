#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Testing multi_ack pack fetching'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Test fetch-pack/upload-pack pair.

# Some convenience functions

add () {
	name=$1 &&
	text="$@" &&
	branch=$(echo $name | sed -e 's/^\(.\).*$/\1/') &&
	parents="" &&

	shift &&
	while test $1; do
		parents="$parents -p $1" &&
		shift
	done &&

	echo "$text" > test.txt &&
	but update-index --add test.txt &&
	tree=$(but write-tree) &&
	# make sure timestamps are in correct order
	test_tick &&
	cummit=$(echo "$text" | but cummit-tree $tree $parents) &&
	eval "$name=$cummit; export $name" &&
	but update-ref "refs/heads/$branch" "$cummit" &&
	eval ${branch}TIP=$cummit
}

pull_to_client () {
	number=$1 &&
	heads=$2 &&
	count=$3 &&
	test_expect_success "$number pull" '
		(
			cd client &&
			but fetch-pack -k -v .. $heads &&

			case "$heads" in
			    *A*)
				    but update-ref refs/heads/A "$ATIP";;
			esac &&
			case "$heads" in *B*)
			    but update-ref refs/heads/B "$BTIP";;
			esac &&

			but symbolic-ref HEAD refs/heads/$(
				echo $heads |
				sed -e "s/^\(.\).*$/\1/"
			) &&

			but fsck --full &&

			mv .but/objects/pack/pack-* . &&
			p=$(ls -1 pack-*.pack) &&
			but unpack-objects <$p &&
			but fsck --full &&

			idx=$(echo pack-*.idx) &&
			pack_count=$(but show-index <$idx | wc -l) &&
			test $pack_count = $count &&
			rm -f pack-*
		)
	'
}

# Here begins the actual testing

# A1 - ... - A20 - A21
#    \
#      B1  -   B2 - .. - B70

# client pulls A20, B1. Then tracks only B. Then pulls A.

test_expect_success 'setup' '
	mkdir client &&
	(
		cd client &&
		but init &&
		but config transfer.unpacklimit 0
	) &&
	add A1 &&
	prev=1 &&
	cur=2 &&
	while [ $cur -le 10 ]; do
		add A$cur $(eval echo \$A$prev) &&
		prev=$cur &&
		cur=$(($cur+1)) || return 1
	done &&
	add B1 $A1 &&
	but update-ref refs/heads/A "$ATIP" &&
	but update-ref refs/heads/B "$BTIP" &&
	but symbolic-ref HEAD refs/heads/B
'

pull_to_client 1st "refs/heads/B refs/heads/A" $((11*3))

test_expect_success 'post 1st pull setup' '
	add A11 $A10 &&
	prev=1 &&
	cur=2 &&
	while [ $cur -le 65 ]; do
		add B$cur $(eval echo \$B$prev) &&
		prev=$cur &&
		cur=$(($cur+1)) || return 1
	done
'

pull_to_client 2nd "refs/heads/B" $((64*3))

pull_to_client 3rd "refs/heads/A" $((1*3))

test_expect_success 'single branch clone' '
	but clone --single-branch "file://$(pwd)/." singlebranch
'

test_expect_success 'single branch object count' '
	BUT_DIR=singlebranch/.but but count-objects -v |
		grep "^in-pack:" > count.singlebranch &&
	echo "in-pack: 198" >expected &&
	test_cmp expected count.singlebranch
'

test_expect_success 'single given branch clone' '
	but clone --single-branch --branch A "file://$(pwd)/." branch-a &&
	test_must_fail but --but-dir=branch-a/.but rev-parse origin/B
'

test_expect_success 'clone shallow depth 1' '
	but clone --no-single-branch --depth 1 "file://$(pwd)/." shallow0 &&
	test "$(but --but-dir=shallow0/.but rev-list --count HEAD)" = 1
'

test_expect_success 'clone shallow depth 1 with fsck' '
	but config --global fetch.fsckobjects true &&
	but clone --no-single-branch --depth 1 "file://$(pwd)/." shallow0fsck &&
	test "$(but --but-dir=shallow0fsck/.but rev-list --count HEAD)" = 1 &&
	but config --global --unset fetch.fsckobjects
'

test_expect_success 'clone shallow' '
	but clone --no-single-branch --depth 2 "file://$(pwd)/." shallow
'

test_expect_success 'clone shallow depth count' '
	test "$(but --but-dir=shallow/.but rev-list --count HEAD)" = 2
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		but count-objects -v
	) > count.shallow &&
	grep "^in-pack: 12" count.shallow
'

test_expect_success 'clone shallow object count (part 2)' '
	sed -e "/^in-pack:/d" -e "/^packs:/d" -e "/^size-pack:/d" \
	    -e "/: 0$/d" count.shallow > count_output &&
	test_must_be_empty count_output
'

test_expect_success 'fsck in shallow repo' '
	(
		cd shallow &&
		but fsck --full
	)
'

test_expect_success 'simple fetch in shallow repo' '
	(
		cd shallow &&
		but fetch
	)
'

test_expect_success 'no changes expected' '
	(
		cd shallow &&
		but count-objects -v
	) > count.shallow.2 &&
	cmp count.shallow count.shallow.2
'

test_expect_success 'fetch same depth in shallow repo' '
	(
		cd shallow &&
		but fetch --depth=2
	)
'

test_expect_success 'no changes expected' '
	(
		cd shallow &&
		but count-objects -v
	) > count.shallow.3 &&
	cmp count.shallow count.shallow.3
'

test_expect_success 'add two more' '
	add B66 $B65 &&
	add B67 $B66
'

test_expect_success 'pull in shallow repo' '
	(
		cd shallow &&
		but pull .. B
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		but count-objects -v
	) > count.shallow &&
	grep "^count: 6" count.shallow
'

test_expect_success 'add two more (part 2)' '
	add B68 $B67 &&
	add B69 $B68
'

test_expect_success 'deepening pull in shallow repo' '
	(
		cd shallow &&
		but pull --depth 4 .. B
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		but count-objects -v
	) > count.shallow &&
	grep "^count: 12" count.shallow
'

test_expect_success 'deepening fetch in shallow repo' '
	(
		cd shallow &&
		but fetch --depth 4 .. A:A
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		but count-objects -v
	) > count.shallow &&
	grep "^count: 18" count.shallow
'

test_expect_success 'pull in shallow repo with missing merge base' '
	(
		cd shallow &&
		but fetch --depth 4 .. A &&
		test_must_fail but merge --allow-unrelated-histories FETCH_HEAD
	)
'

test_expect_success 'additional simple shallow deepenings' '
	(
		cd shallow &&
		but fetch --depth=8 &&
		but fetch --depth=10 &&
		but fetch --depth=11
	)
'

test_expect_success 'clone shallow depth count' '
	test "$(but --but-dir=shallow/.but rev-list --count HEAD)" = 11
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		but prune &&
		but count-objects -v
	) > count.shallow &&
	grep "^count: 54" count.shallow
'

test_expect_success 'fetch --no-shallow on full repo' '
	test_must_fail but fetch --noshallow
'

test_expect_success 'fetch --depth --no-shallow' '
	(
		cd shallow &&
		test_must_fail but fetch --depth=1 --noshallow
	)
'

test_expect_success 'turn shallow to complete repository' '
	(
		cd shallow &&
		but fetch --unshallow &&
		! test -f .but/shallow &&
		but fsck --full
	)
'

test_expect_success 'clone shallow without --no-single-branch' '
	but clone --depth 1 "file://$(pwd)/." shallow2
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow2 &&
		but count-objects -v
	) > count.shallow2 &&
	grep "^in-pack: 3" count.shallow2
'

test_expect_success 'clone shallow with --branch' '
	but clone --depth 1 --branch A "file://$(pwd)/." shallow3
'

test_expect_success 'clone shallow object count' '
	echo "in-pack: 3" > count3.expected &&
	BUT_DIR=shallow3/.but but count-objects -v |
		grep "^in-pack" > count3.actual &&
	test_cmp count3.expected count3.actual
'

test_expect_success 'clone shallow with detached HEAD' '
	but checkout HEAD^ &&
	but clone --depth 1 "file://$(pwd)/." shallow5 &&
	but checkout - &&
	BUT_DIR=shallow5/.but but rev-parse HEAD >actual &&
	but rev-parse HEAD^ >expected &&
	test_cmp expected actual
'

test_expect_success 'shallow clone pulling tags' '
	but tag -a -m A TAGA1 A &&
	but tag -a -m B TAGB1 B &&
	but tag TAGA2 A &&
	but tag TAGB2 B &&
	but clone --depth 1 "file://$(pwd)/." shallow6 &&

	cat >taglist.expected <<\EOF &&
TAGB1
TAGB2
EOF
	BUT_DIR=shallow6/.but but tag -l >taglist.actual &&
	test_cmp taglist.expected taglist.actual &&

	echo "in-pack: 4" > count6.expected &&
	BUT_DIR=shallow6/.but but count-objects -v |
		grep "^in-pack" > count6.actual &&
	test_cmp count6.expected count6.actual
'

test_expect_success 'shallow cloning single tag' '
	but clone --depth 1 --branch=TAGB1 "file://$(pwd)/." shallow7 &&
	cat >taglist.expected <<\EOF &&
TAGB1
TAGB2
EOF
	BUT_DIR=shallow7/.but but tag -l >taglist.actual &&
	test_cmp taglist.expected taglist.actual &&

	echo "in-pack: 4" > count7.expected &&
	BUT_DIR=shallow7/.but but count-objects -v |
		grep "^in-pack" > count7.actual &&
	test_cmp count7.expected count7.actual
'

test_expect_success 'clone shallow with packed refs' '
	but pack-refs --all &&
	but clone --depth 1 --branch A "file://$(pwd)/." shallow8 &&
	echo "in-pack: 4" > count8.expected &&
	BUT_DIR=shallow8/.but but count-objects -v |
		grep "^in-pack" > count8.actual &&
	test_cmp count8.expected count8.actual
'

test_expect_success 'in_vain not triggered before first ACK' '
	rm -rf myserver myclient &&
	but init myserver &&
	test_cummit -C myserver foo &&
	but clone "file://$(pwd)/myserver" myclient &&

	# MAX_IN_VAIN is 256. Because of batching, the client will send 496
	# (16+32+64+128+256) cummits, not 256, before giving up. So create 496
	# irrelevant cummits.
	test_cummit_bulk -C myclient 496 &&

	# The new cummit that the client wants to fetch.
	test_cummit -C myserver bar &&

	but -C myclient fetch --progress origin 2>log &&
	test_i18ngrep "remote: Total 3 " log
'

test_expect_success 'in_vain resetted upon ACK' '
	rm -rf myserver myclient &&
	but init myserver &&

	# Linked list of cummits on main. The first is common; the rest are
	# not.
	test_cummit -C myserver first_main_cummit &&
	but clone "file://$(pwd)/myserver" myclient &&
	test_cummit_bulk -C myclient 255 &&

	# Another linked list of cummits on anotherbranch with no connection to
	# main. The first is common; the rest are not.
	but -C myserver checkout --orphan anotherbranch &&
	test_cummit -C myserver first_anotherbranch_cummit &&
	but -C myclient fetch origin anotherbranch:refs/heads/anotherbranch &&
	but -C myclient checkout anotherbranch &&
	test_cummit_bulk -C myclient 255 &&

	# The new cummit that the client wants to fetch.
	but -C myserver checkout main &&
	test_cummit -C myserver to_fetch &&

	# The client will send (as "have"s) all 256 cummits in anotherbranch
	# first. The 256th cummit is common between the client and the server,
	# and should reset in_vain. This allows negotiation to continue until
	# the client reports that first_anotherbranch_cummit is common.
	but -C myclient fetch --progress origin main 2>log &&
	test_i18ngrep "Total 3 " log
'

test_expect_success 'fetch in shallow repo unreachable shallow objects' '
	(
		but clone --bare --branch B --single-branch "file://$(pwd)/." no-reflog &&
		but clone --depth 1 "file://$(pwd)/no-reflog" shallow9 &&
		cd no-reflog &&
		but tag -d TAGB1 TAGB2 &&
		but update-ref refs/heads/B B~~ &&
		but gc --prune=now &&
		cd ../shallow9 &&
		but fetch origin &&
		but fsck --no-dangling
	)
'
test_expect_success 'fetch creating new shallow root' '
	(
		but clone "file://$(pwd)/." shallow10 &&
		but cummit --allow-empty -m empty &&
		cd shallow10 &&
		but fetch --depth=1 --progress 2>actual &&
		# This should fetch only the empty cummit, no tree or
		# blob objects
		test_i18ngrep "remote: Total 1" actual
	)
'

test_expect_success 'setup tests for the --stdin parameter' '
	for head in C D E F
	do
		add $head || return 1
	done &&
	for head in A B C D E F
	do
		but tag $head $head || return 1
	done &&
	cat >input <<-\EOF &&
	refs/heads/C
	refs/heads/A
	refs/heads/D
	refs/tags/C
	refs/heads/B
	refs/tags/A
	refs/heads/E
	refs/tags/B
	refs/tags/E
	refs/tags/D
	EOF
	sort <input >expect &&
	(
		echo refs/heads/E &&
		echo refs/tags/E &&
		cat input
	) >input.dup
'

test_expect_success 'setup fetch refs from cmdline v[12]' '
	cp -r client client0 &&
	cp -r client client1 &&
	cp -r client client2
'

for version in '' 0 1 2
do
	test_expect_success "protocol.version=$version fetch refs from cmdline" "
		(
			cd client$version &&
			BUT_TEST_PROTOCOL_VERSION=$version but fetch-pack --no-progress .. \$(cat ../input)
		) >output &&
		cut -d ' ' -f 2 <output | sort >actual &&
		test_cmp expect actual
	"
done

test_expect_success 'fetch refs from stdin' '
	(
		cd client &&
		but fetch-pack --stdin --no-progress .. <../input
	) >output &&
	cut -d " " -f 2 <output | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch mixed refs from cmdline and stdin' '
	(
		cd client &&
		tail -n +5 ../input |
		but fetch-pack --stdin --no-progress .. $(head -n 4 ../input)
	) >output &&
	cut -d " " -f 2 <output | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'test duplicate refs from stdin' '
	(
	cd client &&
	but fetch-pack --stdin --no-progress .. <../input.dup
	) >output &&
	cut -d " " -f 2 <output | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'set up tests of missing reference' '
	cat >expect-error <<-\EOF
	error: no such remote ref refs/heads/xyzzy
	EOF
'

test_expect_success 'test lonely missing ref' '
	(
		cd client &&
		test_must_fail but fetch-pack --no-progress .. refs/heads/xyzzy 2>../error-m
	) &&
	test_cmp expect-error error-m
'

test_expect_success 'test missing ref after existing' '
	(
		cd client &&
		test_must_fail but fetch-pack --no-progress .. refs/heads/A refs/heads/xyzzy 2>../error-em
	) &&
	test_cmp expect-error error-em
'

test_expect_success 'test missing ref before existing' '
	(
		cd client &&
		test_must_fail but fetch-pack --no-progress .. refs/heads/xyzzy refs/heads/A 2>../error-me
	) &&
	test_cmp expect-error error-me
'

test_expect_success 'test --all, --depth, and explicit head' '
	(
		cd client &&
		but fetch-pack --no-progress --all --depth=1 .. refs/heads/A
	) >out-adh 2>error-adh
'

test_expect_success 'test --all, --depth, and explicit tag' '
	but tag OLDTAG refs/heads/B~5 &&
	(
		cd client &&
		but fetch-pack --no-progress --all --depth=1 .. refs/tags/OLDTAG
	) >out-adt 2>error-adt
'

test_expect_success 'test --all with tag to non-tip' '
	but cummit --allow-empty -m non-tip &&
	but cummit --allow-empty -m tip &&
	but tag -m "annotated" non-tip HEAD^ &&
	(
		cd client &&
		but fetch-pack --all ..
	)
'

test_expect_success 'test --all wrt tag to non-cummits' '
	# create tag-to-{blob,tree,cummit,tag}, making sure all tagged objects
	# are reachable only via created tag references.
	blob=$(echo "hello blob" | but hash-object -t blob -w --stdin) &&
	but tag -a -m "tag -> blob" tag-to-blob $blob &&

	tree=$(printf "100644 blob $blob\tfile" | but mktree) &&
	but tag -a -m "tag -> tree" tag-to-tree $tree &&

	tree2=$(printf "100644 blob $blob\tfile2" | but mktree) &&
	cummit=$(but cummit-tree -m "hello cummit" $tree) &&
	but tag -a -m "tag -> cummit" tag-to-cummit $cummit &&

	blob2=$(echo "hello blob2" | but hash-object -t blob -w --stdin) &&
	tag=$(but mktag <<-EOF
		object $blob2
		type blob
		tag tag-to-blob2
		tagger author A U Thor <author@example.com> 0 +0000

		hello tag
	EOF
	) &&
	but tag -a -m "tag -> tag" tag-to-tag $tag &&

	# `fetch-pack --all` should succeed fetching all those objects.
	mkdir fetchall &&
	(
		cd fetchall &&
		but init &&
		but fetch-pack --all .. &&
		but cat-file blob $blob >/dev/null &&
		but cat-file tree $tree >/dev/null &&
		but cat-file cummit $cummit >/dev/null &&
		but cat-file tag $tag >/dev/null
	)
'

test_expect_success 'shallow fetch with tags does not break the repository' '
	mkdir repo1 &&
	(
		cd repo1 &&
		but init &&
		test_cummit 1 &&
		test_cummit 2 &&
		test_cummit 3 &&
		mkdir repo2 &&
		cd repo2 &&
		but init &&
		but fetch --depth=2 ../.but main:branch &&
		but fsck
	)
'

test_expect_success 'fetch-pack can fetch a raw sha1' '
	but init hidden &&
	(
		cd hidden &&
		test_cummit 1 &&
		test_cummit 2 &&
		but update-ref refs/hidden/one HEAD^ &&
		but config transfer.hiderefs refs/hidden &&
		but config uploadpack.allowtipsha1inwant true
	) &&
	but fetch-pack hidden $(but -C hidden rev-parse refs/hidden/one)
'

test_expect_success 'fetch-pack can fetch a raw sha1 that is advertised as a ref' '
	rm -rf server client &&
	but init server &&
	test_cummit -C server 1 &&

	but init client &&
	but -C client fetch-pack ../server \
		$(but -C server rev-parse refs/heads/main)
'

test_expect_success 'fetch-pack can fetch a raw sha1 overlapping a named ref' '
	rm -rf server client &&
	but init server &&
	test_cummit -C server 1 &&
	test_cummit -C server 2 &&

	but init client &&
	but -C client fetch-pack ../server \
		$(but -C server rev-parse refs/tags/1) refs/tags/1
'

test_expect_success 'fetch-pack cannot fetch a raw sha1 that is not advertised as a ref' '
	rm -rf server &&

	but init server &&
	test_cummit -C server 5 &&
	but -C server tag -d 5 &&
	test_cummit -C server 6 &&

	but init client &&
	# Some protocol versions (e.g. 2) support fetching
	# unadvertised objects, so restrict this test to v0.
	test_must_fail env BUT_TEST_PROTOCOL_VERSION=0 but -C client fetch-pack ../server \
		$(but -C server rev-parse refs/heads/main^) 2>err &&
	test_i18ngrep "Server does not allow request for unadvertised object" err
'

check_prot_path () {
	cat >expected <<-EOF &&
	Diag: url=$1
	Diag: protocol=$2
	Diag: path=$3
	EOF
	but fetch-pack --diag-url "$1" | grep -v hostandport= >actual &&
	test_cmp expected actual
}

check_prot_host_port_path () {
	case "$2" in
		*ssh*)
		pp=ssh
		uah=userandhost
		ehost=$(echo $3 | tr -d "[]")
		diagport="Diag: port=$4"
		;;
		*)
		pp=$p
		uah=hostandport
		ehost=$(echo $3$4 | sed -e "s/22$/:22/" -e "s/NONE//")
		diagport=""
		;;
	esac
	cat >exp <<-EOF &&
	Diag: url=$1
	Diag: protocol=$pp
	Diag: $uah=$ehost
	$diagport
	Diag: path=$5
	EOF
	grep -v "^$" exp >expected
	but fetch-pack --diag-url "$1" >actual &&
	test_cmp expected actual
}

for r in repo re:po re/po
do
	# but or ssh with scheme
	for p in "ssh+but" "but+ssh" but ssh
	do
		for h in host user@host user@[::1] user@::1
		do
			for c in "" :
			do
				test_expect_success "fetch-pack --diag-url $p://$h$c/$r" '
					check_prot_host_port_path $p://$h/$r $p "$h" NONE "/$r"
				'
				# "/~" -> "~" conversion
				test_expect_success "fetch-pack --diag-url $p://$h$c/~$r" '
					check_prot_host_port_path $p://$h/~$r $p "$h" NONE "~$r"
				'
			done
		done
		for h in host User@host User@[::1]
		do
			test_expect_success "fetch-pack --diag-url $p://$h:22/$r" '
				check_prot_host_port_path $p://$h:22/$r $p "$h" 22 "/$r"
			'
		done
	done
	# file with scheme
	for p in file
	do
		test_expect_success !MINGW "fetch-pack --diag-url $p://$h/$r" '
			check_prot_path $p://$h/$r $p "/$r"
		'
		test_expect_success MINGW "fetch-pack --diag-url $p://$h/$r" '
			check_prot_path $p://$h/$r $p "//$h/$r"
		'
		test_expect_success MINGW "fetch-pack --diag-url $p:///$r" '
			check_prot_path $p:///$r $p "/$r"
		'
		# No "/~" -> "~" conversion for file
		test_expect_success !MINGW "fetch-pack --diag-url $p://$h/~$r" '
			check_prot_path $p://$h/~$r $p "/~$r"
		'
		test_expect_success MINGW "fetch-pack --diag-url $p://$h/~$r" '
			check_prot_path $p://$h/~$r $p "//$h/~$r"
		'
	done
	# file without scheme
	for h in nohost nohost:12 [::1] [::1]:23 [ [:aa
	do
		test_expect_success "fetch-pack --diag-url ./$h:$r" '
			check_prot_path ./$h:$r $p "./$h:$r"
		'
		# No "/~" -> "~" conversion for file
		test_expect_success "fetch-pack --diag-url ./$p:$h/~$r" '
		check_prot_path ./$p:$h/~$r $p "./$p:$h/~$r"
		'
	done
	#ssh without scheme
	p=ssh
	for h in host [::1]
	do
		test_expect_success "fetch-pack --diag-url $h:$r" '
			check_prot_host_port_path $h:$r $p "$h" NONE "$r"
		'
		# Do "/~" -> "~" conversion
		test_expect_success "fetch-pack --diag-url $h:/~$r" '
			check_prot_host_port_path $h:/~$r $p "$h" NONE "~$r"
		'
	done
done

test_expect_success MINGW 'fetch-pack --diag-url file://c:/repo' '
	check_prot_path file://c:/repo file c:/repo
'
test_expect_success MINGW 'fetch-pack --diag-url c:repo' '
	check_prot_path c:repo file c:repo
'

test_expect_success 'clone shallow since ...' '
	test_create_repo shallow-since &&
	(
	cd shallow-since &&
	BUT_CUMMITTER_DATE="100000000 +0700" but cummit --allow-empty -m one &&
	BUT_CUMMITTER_DATE="200000000 +0700" but cummit --allow-empty -m two &&
	BUT_CUMMITTER_DATE="300000000 +0700" but cummit --allow-empty -m three &&
	but clone --shallow-since "300000000 +0700" "file://$(pwd)/." ../shallow11 &&
	but -C ../shallow11 log --pretty=tformat:%s HEAD >actual &&
	echo three >expected &&
	test_cmp expected actual
	)
'

test_expect_success 'fetch shallow since ...' '
	but -C shallow11 fetch --shallow-since "200000000 +0700" origin &&
	but -C shallow11 log --pretty=tformat:%s origin/main >actual &&
	cat >expected <<-\EOF &&
	three
	two
	EOF
	test_cmp expected actual
'

test_expect_success 'clone shallow since selects no cummits' '
	test_create_repo shallow-since-the-future &&
	(
	cd shallow-since-the-future &&
	BUT_CUMMITTER_DATE="100000000 +0700" but cummit --allow-empty -m one &&
	BUT_CUMMITTER_DATE="200000000 +0700" but cummit --allow-empty -m two &&
	BUT_CUMMITTER_DATE="300000000 +0700" but cummit --allow-empty -m three &&
	test_must_fail but clone --shallow-since "900000000 +0700" "file://$(pwd)/." ../shallow111
	)
'

# A few subtle things about the request in this test:
#
#  - the server must have cummit-graphs present and enabled
#
#  - the history is such that our want/have share a common ancestor ("base"
#    here)
#
#  - we send only a single have, which is fewer than a normal client would
#    send. This ensures that we don't parse "base" up front with
#    parse_object(), but rather traverse to it as a parent while deciding if we
#    can stop the "have" negotiation, and call parse_cummit(). The former
#    sees the actual object data and so always loads the three oid, whereas the
#    latter will try to load it lazily.
#
#  - we must use protocol v2, because it handles the "have" negotiation before
#    processing the shallow directives
#
test_expect_success 'shallow since with cummit graph and already-seen cummit' '
	test_create_repo shallow-since-graph &&
	(
	cd shallow-since-graph &&
	test_cummit base &&
	test_cummit main &&
	but checkout -b other HEAD^ &&
	test_cummit other &&
	but cummit-graph write --reachable &&
	but config core.cummitGraph true &&

	BUT_PROTOCOL=version=2 but upload-pack . <<-EOF >/dev/null
	0012command=fetch
	$(echo "object-format=$(test_oid algo)" | packetize)
	00010013deepen-since 1
	$(echo "want $(but rev-parse other)" | packetize)
	$(echo "have $(but rev-parse main)" | packetize)
	0000
	EOF
	)
'

test_expect_success 'shallow clone exclude tag two' '
	test_create_repo shallow-exclude &&
	(
	cd shallow-exclude &&
	test_cummit one &&
	test_cummit two &&
	test_cummit three &&
	but clone --shallow-exclude two "file://$(pwd)/." ../shallow12 &&
	but -C ../shallow12 log --pretty=tformat:%s HEAD >actual &&
	echo three >expected &&
	test_cmp expected actual
	)
'

test_expect_success 'fetch exclude tag one' '
	but -C shallow12 fetch --shallow-exclude one origin &&
	but -C shallow12 log --pretty=tformat:%s origin/main >actual &&
	test_write_lines three two >expected &&
	test_cmp expected actual
'

test_expect_success 'fetching deepen' '
	test_create_repo shallow-deepen &&
	(
	cd shallow-deepen &&
	test_cummit one &&
	test_cummit two &&
	test_cummit three &&
	but clone --depth 1 "file://$(pwd)/." deepen &&
	test_cummit four &&
	but -C deepen log --pretty=tformat:%s main >actual &&
	echo three >expected &&
	test_cmp expected actual &&
	but -C deepen fetch --deepen=1 &&
	but -C deepen log --pretty=tformat:%s origin/main >actual &&
	cat >expected <<-\EOF &&
	four
	three
	two
	EOF
	test_cmp expected actual
	)
'

test_negotiation_algorithm_default () {
	test_when_finished rm -rf clientv0 clientv2 &&
	rm -rf server client &&
	but init server &&
	test_cummit -C server both_have_1 &&
	but -C server tag -d both_have_1 &&
	test_cummit -C server both_have_2 &&

	but clone server client &&
	test_cummit -C server server_has &&
	test_cummit -C client client_has &&

	# In both protocol v0 and v2, ensure that the parent of both_have_2 is
	# not sent as a "have" line. The client should know that the server has
	# both_have_2, so it only needs to inform the server that it has
	# both_have_2, and the server can infer the rest.

	rm -f trace &&
	cp -r client clientv0 &&
	BUT_TRACE_PACKET="$(pwd)/trace" but -C clientv0 \
		"$@" fetch origin server_has both_have_2 &&
	grep "have $(but -C client rev-parse client_has)" trace &&
	grep "have $(but -C client rev-parse both_have_2)" trace &&
	! grep "have $(but -C client rev-parse both_have_2^)" trace &&

	rm -f trace &&
	cp -r client clientv2 &&
	BUT_TRACE_PACKET="$(pwd)/trace" but -C clientv2 -c protocol.version=2 \
		"$@" fetch origin server_has both_have_2 &&
	grep "have $(but -C client rev-parse client_has)" trace &&
	grep "have $(but -C client rev-parse both_have_2)" trace &&
	! grep "have $(but -C client rev-parse both_have_2^)" trace
}

test_expect_success 'use ref advertisement to prune "have" lines sent' '
	test_negotiation_algorithm_default
'

test_expect_success 'same as last but with config overrides' '
	test_negotiation_algorithm_default \
		-c feature.experimental=true \
		-c fetch.negotiationAlgorithm=consecutive
'

test_expect_success 'ensure bogus fetch.negotiationAlgorithm yields error' '
	test_when_finished rm -rf clientv0 &&
	cp -r client clientv0 &&
	test_must_fail but -C clientv0 --fetch.negotiationAlgorithm=bogus \
		       fetch origin server_has both_have_2
'

test_expect_success 'filtering by size' '
	rm -rf server client &&
	test_create_repo server &&
	test_cummit -C server one &&
	test_config -C server uploadpack.allowfilter 1 &&

	test_create_repo client &&
	but -C client fetch-pack --filter=blob:limit=0 ../server HEAD &&

	# Ensure that object is not inadvertently fetched
	cummit=$(but -C server rev-parse HEAD) &&
	blob=$(but hash-object server/one.t) &&
	but -C client rev-list --objects --missing=allow-any "$cummit" >oids &&
	! grep "$blob" oids
'

test_expect_success 'filtering by size has no effect if support for it is not advertised' '
	rm -rf server client &&
	test_create_repo server &&
	test_cummit -C server one &&

	test_create_repo client &&
	but -C client fetch-pack --filter=blob:limit=0 ../server HEAD 2> err &&

	# Ensure that object is fetched
	cummit=$(but -C server rev-parse HEAD) &&
	blob=$(but hash-object server/one.t) &&
	but -C client rev-list --objects --missing=allow-any "$cummit" >oids &&
	grep "$blob" oids &&

	test_i18ngrep "filtering not recognized by server" err
'

fetch_filter_blob_limit_zero () {
	SERVER="$1"
	URL="$2"

	rm -rf "$SERVER" client &&
	test_create_repo "$SERVER" &&
	test_cummit -C "$SERVER" one &&
	test_config -C "$SERVER" uploadpack.allowfilter 1 &&

	but clone "$URL" client &&

	test_cummit -C "$SERVER" two &&

	but -C client fetch --filter=blob:limit=0 origin HEAD:somewhere &&

	# Ensure that cummit is fetched, but blob is not
	cummit=$(but -C "$SERVER" rev-parse two) &&
	blob=$(but hash-object server/two.t) &&
	but -C client rev-list --objects --missing=allow-any "$cummit" >oids &&
	grep "$cummit" oids &&
	! grep "$blob" oids
}

test_expect_success 'fetch with --filter=blob:limit=0' '
	fetch_filter_blob_limit_zero server server
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'fetch with --filter=blob:limit=0 and HTTP' '
	fetch_filter_blob_limit_zero "$HTTPD_DOCUMENT_ROOT_PATH/server" "$HTTPD_URL/smart/server"
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
