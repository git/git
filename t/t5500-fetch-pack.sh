#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Testing multi_ack pack fetching'

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
	git update-index --add test.txt &&
	tree=$(git write-tree) &&
	# make sure timestamps are in correct order
	test_tick &&
	commit=$(echo "$text" | git commit-tree $tree $parents) &&
	eval "$name=$commit; export $name" &&
	echo $commit > .git/refs/heads/$branch &&
	eval ${branch}TIP=$commit
}

pull_to_client () {
	number=$1 &&
	heads=$2 &&
	count=$3 &&
	test_expect_success "$number pull" '
		(
			cd client &&
			git fetch-pack -k -v .. $heads &&

			case "$heads" in
			    *A*)
				    echo $ATIP > .git/refs/heads/A;;
			esac &&
			case "$heads" in *B*)
			    echo $BTIP > .git/refs/heads/B;;
			esac &&
			git symbolic-ref HEAD refs/heads/$(echo $heads \
				| sed -e "s/^\(.\).*$/\1/") &&

			git fsck --full &&

			mv .git/objects/pack/pack-* . &&
			p=$(ls -1 pack-*.pack) &&
			git unpack-objects <$p &&
			git fsck --full &&

			idx=$(echo pack-*.idx) &&
			pack_count=$(git show-index <$idx | wc -l) &&
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
		git init &&
		git config transfer.unpacklimit 0
	) &&
	add A1 &&
	prev=1 &&
	cur=2 &&
	while [ $cur -le 10 ]; do
		add A$cur $(eval echo \$A$prev) &&
		prev=$cur &&
		cur=$(($cur+1))
	done &&
	add B1 $A1 &&
	echo $ATIP > .git/refs/heads/A &&
	echo $BTIP > .git/refs/heads/B &&
	git symbolic-ref HEAD refs/heads/B
'

pull_to_client 1st "refs/heads/B refs/heads/A" $((11*3))

test_expect_success 'post 1st pull setup' '
	add A11 $A10 &&
	prev=1 &&
	cur=2 &&
	while [ $cur -le 65 ]; do
		add B$cur $(eval echo \$B$prev) &&
		prev=$cur &&
		cur=$(($cur+1))
	done
'

pull_to_client 2nd "refs/heads/B" $((64*3))

pull_to_client 3rd "refs/heads/A" $((1*3))

test_expect_success 'single branch clone' '
	git clone --single-branch "file://$(pwd)/." singlebranch
'

test_expect_success 'single branch object count' '
	GIT_DIR=singlebranch/.git git count-objects -v |
		grep "^in-pack:" > count.singlebranch &&
	echo "in-pack: 198" >expected &&
	test_cmp expected count.singlebranch
'

test_expect_success 'single given branch clone' '
	git clone --single-branch --branch A "file://$(pwd)/." branch-a &&
	test_must_fail git --git-dir=branch-a/.git rev-parse origin/B
'

test_expect_success 'clone shallow depth 1' '
	git clone --no-single-branch --depth 1 "file://$(pwd)/." shallow0 &&
	test "$(git --git-dir=shallow0/.git rev-list --count HEAD)" = 1
'

test_expect_success 'clone shallow depth 1 with fsck' '
	git config --global fetch.fsckobjects true &&
	git clone --no-single-branch --depth 1 "file://$(pwd)/." shallow0fsck &&
	test "$(git --git-dir=shallow0fsck/.git rev-list --count HEAD)" = 1 &&
	git config --global --unset fetch.fsckobjects
'

test_expect_success 'clone shallow' '
	git clone --no-single-branch --depth 2 "file://$(pwd)/." shallow
'

test_expect_success 'clone shallow depth count' '
	test "$(git --git-dir=shallow/.git rev-list --count HEAD)" = 2
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow &&
	grep "^in-pack: 12" count.shallow
'

test_expect_success 'clone shallow object count (part 2)' '
	sed -e "/^in-pack:/d" -e "/^packs:/d" -e "/^size-pack:/d" \
	    -e "/: 0$/d" count.shallow > count_output &&
	! test -s count_output
'

test_expect_success 'fsck in shallow repo' '
	(
		cd shallow &&
		git fsck --full
	)
'

test_expect_success 'simple fetch in shallow repo' '
	(
		cd shallow &&
		git fetch
	)
'

test_expect_success 'no changes expected' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow.2 &&
	cmp count.shallow count.shallow.2
'

test_expect_success 'fetch same depth in shallow repo' '
	(
		cd shallow &&
		git fetch --depth=2
	)
'

test_expect_success 'no changes expected' '
	(
		cd shallow &&
		git count-objects -v
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
		git pull .. B
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
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
		git pull --depth 4 .. B
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow &&
	grep "^count: 12" count.shallow
'

test_expect_success 'deepening fetch in shallow repo' '
	(
		cd shallow &&
		git fetch --depth 4 .. A:A
	)
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git count-objects -v
	) > count.shallow &&
	grep "^count: 18" count.shallow
'

test_expect_success 'pull in shallow repo with missing merge base' '
	(
		cd shallow &&
		git fetch --depth 4 .. A
		test_must_fail git merge --allow-unrelated-histories FETCH_HEAD
	)
'

test_expect_success 'additional simple shallow deepenings' '
	(
		cd shallow &&
		git fetch --depth=8 &&
		git fetch --depth=10 &&
		git fetch --depth=11
	)
'

test_expect_success 'clone shallow depth count' '
	test "$(git --git-dir=shallow/.git rev-list --count HEAD)" = 11
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow &&
		git prune &&
		git count-objects -v
	) > count.shallow &&
	grep "^count: 54" count.shallow
'

test_expect_success 'fetch --no-shallow on full repo' '
	test_must_fail git fetch --noshallow
'

test_expect_success 'fetch --depth --no-shallow' '
	(
		cd shallow &&
		test_must_fail git fetch --depth=1 --noshallow
	)
'

test_expect_success 'turn shallow to complete repository' '
	(
		cd shallow &&
		git fetch --unshallow &&
		! test -f .git/shallow &&
		git fsck --full
	)
'

test_expect_success 'clone shallow without --no-single-branch' '
	git clone --depth 1 "file://$(pwd)/." shallow2
'

test_expect_success 'clone shallow object count' '
	(
		cd shallow2 &&
		git count-objects -v
	) > count.shallow2 &&
	grep "^in-pack: 3" count.shallow2
'

test_expect_success 'clone shallow with --branch' '
	git clone --depth 1 --branch A "file://$(pwd)/." shallow3
'

test_expect_success 'clone shallow object count' '
	echo "in-pack: 3" > count3.expected &&
	GIT_DIR=shallow3/.git git count-objects -v |
		grep "^in-pack" > count3.actual &&
	test_cmp count3.expected count3.actual
'

test_expect_success 'clone shallow with detached HEAD' '
	git checkout HEAD^ &&
	git clone --depth 1 "file://$(pwd)/." shallow5 &&
	git checkout - &&
	GIT_DIR=shallow5/.git git rev-parse HEAD >actual &&
	git rev-parse HEAD^ >expected &&
	test_cmp expected actual
'

test_expect_success 'shallow clone pulling tags' '
	git tag -a -m A TAGA1 A &&
	git tag -a -m B TAGB1 B &&
	git tag TAGA2 A &&
	git tag TAGB2 B &&
	git clone --depth 1 "file://$(pwd)/." shallow6 &&

	cat >taglist.expected <<\EOF &&
TAGB1
TAGB2
EOF
	GIT_DIR=shallow6/.git git tag -l >taglist.actual &&
	test_cmp taglist.expected taglist.actual &&

	echo "in-pack: 4" > count6.expected &&
	GIT_DIR=shallow6/.git git count-objects -v |
		grep "^in-pack" > count6.actual &&
	test_cmp count6.expected count6.actual
'

test_expect_success 'shallow cloning single tag' '
	git clone --depth 1 --branch=TAGB1 "file://$(pwd)/." shallow7 &&
	cat >taglist.expected <<\EOF &&
TAGB1
TAGB2
EOF
	GIT_DIR=shallow7/.git git tag -l >taglist.actual &&
	test_cmp taglist.expected taglist.actual &&

	echo "in-pack: 4" > count7.expected &&
	GIT_DIR=shallow7/.git git count-objects -v |
		grep "^in-pack" > count7.actual &&
	test_cmp count7.expected count7.actual
'

test_expect_success 'clone shallow with packed refs' '
	git pack-refs --all &&
	git clone --depth 1 --branch A "file://$(pwd)/." shallow8 &&
	echo "in-pack: 4" > count8.expected &&
	GIT_DIR=shallow8/.git git count-objects -v |
		grep "^in-pack" > count8.actual &&
	test_cmp count8.expected count8.actual
'

test_expect_success 'fetch in shallow repo unreachable shallow objects' '
	(
		git clone --bare --branch B --single-branch "file://$(pwd)/." no-reflog &&
		git clone --depth 1 "file://$(pwd)/no-reflog" shallow9 &&
		cd no-reflog &&
		git tag -d TAGB1 TAGB2 &&
		git update-ref refs/heads/B B~~ &&
		git gc --prune=now &&
		cd ../shallow9 &&
		git fetch origin &&
		git fsck --no-dangling
	)
'
test_expect_success 'fetch creating new shallow root' '
	(
		git clone "file://$(pwd)/." shallow10 &&
		git commit --allow-empty -m empty &&
		cd shallow10 &&
		git fetch --depth=1 --progress 2>actual &&
		# This should fetch only the empty commit, no tree or
		# blob objects
		grep "remote: Total 1" actual
	)
'

test_expect_success 'setup tests for the --stdin parameter' '
	for head in C D E F
	do
		add $head
	done &&
	for head in A B C D E F
	do
		git tag $head $head
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

test_expect_success 'fetch refs from cmdline' '
	(
		cd client &&
		git fetch-pack --no-progress .. $(cat ../input)
	) >output &&
	cut -d " " -f 2 <output | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch refs from stdin' '
	(
		cd client &&
		git fetch-pack --stdin --no-progress .. <../input
	) >output &&
	cut -d " " -f 2 <output | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch mixed refs from cmdline and stdin' '
	(
		cd client &&
		tail -n +5 ../input |
		git fetch-pack --stdin --no-progress .. $(head -n 4 ../input)
	) >output &&
	cut -d " " -f 2 <output | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'test duplicate refs from stdin' '
	(
	cd client &&
	git fetch-pack --stdin --no-progress .. <../input.dup
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
		test_must_fail git fetch-pack --no-progress .. refs/heads/xyzzy
	) >/dev/null 2>error-m &&
	test_cmp expect-error error-m
'

test_expect_success 'test missing ref after existing' '
	(
		cd client &&
		test_must_fail git fetch-pack --no-progress .. refs/heads/A refs/heads/xyzzy
	) >/dev/null 2>error-em &&
	test_cmp expect-error error-em
'

test_expect_success 'test missing ref before existing' '
	(
		cd client &&
		test_must_fail git fetch-pack --no-progress .. refs/heads/xyzzy refs/heads/A
	) >/dev/null 2>error-me &&
	test_cmp expect-error error-me
'

test_expect_success 'test --all, --depth, and explicit head' '
	(
		cd client &&
		git fetch-pack --no-progress --all --depth=1 .. refs/heads/A
	) >out-adh 2>error-adh
'

test_expect_success 'test --all, --depth, and explicit tag' '
	git tag OLDTAG refs/heads/B~5 &&
	(
		cd client &&
		git fetch-pack --no-progress --all --depth=1 .. refs/tags/OLDTAG
	) >out-adt 2>error-adt
'

test_expect_success 'shallow fetch with tags does not break the repository' '
	mkdir repo1 &&
	(
		cd repo1 &&
		git init &&
		test_commit 1 &&
		test_commit 2 &&
		test_commit 3 &&
		mkdir repo2 &&
		cd repo2 &&
		git init &&
		git fetch --depth=2 ../.git master:branch &&
		git fsck
	)
'

test_expect_success 'fetch-pack can fetch a raw sha1' '
	git init hidden &&
	(
		cd hidden &&
		test_commit 1 &&
		test_commit 2 &&
		git update-ref refs/hidden/one HEAD^ &&
		git config transfer.hiderefs refs/hidden &&
		git config uploadpack.allowtipsha1inwant true
	) &&
	git fetch-pack hidden $(git -C hidden rev-parse refs/hidden/one)
'

check_prot_path () {
	cat >expected <<-EOF &&
	Diag: url=$1
	Diag: protocol=$2
	Diag: path=$3
	EOF
	git fetch-pack --diag-url "$1" | grep -v hostandport= >actual &&
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
	git fetch-pack --diag-url "$1" >actual &&
	test_cmp expected actual
}

for r in repo re:po re/po
do
	# git or ssh with scheme
	for p in "ssh+git" "git+ssh" git ssh
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
		test_expect_success "fetch-pack --diag-url $p://$h/$r" '
			check_prot_path $p://$h/$r $p "/$r"
		'
		# No "/~" -> "~" conversion for file
		test_expect_success "fetch-pack --diag-url $p://$h/~$r" '
			check_prot_path $p://$h/~$r $p "/~$r"
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
	GIT_COMMITTER_DATE="100000000 +0700" git commit --allow-empty -m one &&
	GIT_COMMITTER_DATE="200000000 +0700" git commit --allow-empty -m two &&
	GIT_COMMITTER_DATE="300000000 +0700" git commit --allow-empty -m three &&
	git clone --shallow-since "300000000 +0700" "file://$(pwd)/." ../shallow11 &&
	git -C ../shallow11 log --pretty=tformat:%s HEAD >actual &&
	echo three >expected &&
	test_cmp expected actual
	)
'

test_expect_success 'fetch shallow since ...' '
	git -C shallow11 fetch --shallow-since "200000000 +0700" origin &&
	git -C shallow11 log --pretty=tformat:%s origin/master >actual &&
	cat >expected <<-\EOF &&
	three
	two
	EOF
	test_cmp expected actual
'

test_expect_success 'shallow clone exclude tag two' '
	test_create_repo shallow-exclude &&
	(
	cd shallow-exclude &&
	test_commit one &&
	test_commit two &&
	test_commit three &&
	git clone --shallow-exclude two "file://$(pwd)/." ../shallow12 &&
	git -C ../shallow12 log --pretty=tformat:%s HEAD >actual &&
	echo three >expected &&
	test_cmp expected actual
	)
'

test_expect_success 'fetch exclude tag one' '
	git -C shallow12 fetch --shallow-exclude one origin &&
	git -C shallow12 log --pretty=tformat:%s origin/master >actual &&
	test_write_lines three two >expected &&
	test_cmp expected actual
'

test_expect_success 'fetching deepen' '
	test_create_repo shallow-deepen &&
	(
	cd shallow-deepen &&
	test_commit one &&
	test_commit two &&
	test_commit three &&
	git clone --depth 1 "file://$(pwd)/." deepen &&
	test_commit four &&
	git -C deepen log --pretty=tformat:%s master >actual &&
	echo three >expected &&
	test_cmp expected actual &&
	git -C deepen fetch --deepen=1 &&
	git -C deepen log --pretty=tformat:%s origin/master >actual &&
	cat >expected <<-\EOF &&
	four
	three
	two
	EOF
	test_cmp expected actual
	)
'

test_done
