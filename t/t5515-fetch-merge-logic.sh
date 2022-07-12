#!/bin/sh
#
# Copyright (c) 2007 Santi Béjar, based on t4013 by Junio C Hamano
#
#

test_description='Merge logic in fetch'

# NEEDSWORK: If the overspecification of the expected result is reduced, we
# might be able to run this test in all protocol versions.
GIT_TEST_PROTOCOL_VERSION=0
export GIT_TEST_PROTOCOL_VERSION

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

build_script () {
	script="$1" &&
	for i in one three_file main topic_2 one_tree three two two2 three2
	do
		echo "s/$(test_oid --hash=sha1 "$i")/$(test_oid "$i")/g" >>"$script"
	done
}

convert_expected () {
	file="$1" &&
	script="$2" &&
	sed -f "$script" "$file" >"$file.tmp" &&
	mv "$file.tmp" "$file"
}

test_expect_success setup '
	GIT_AUTHOR_DATE="2006-06-26 00:00:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:00:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	test_oid_cache <<-EOF &&
	one sha1:8e32a6d901327a23ef831511badce7bf3bf46689
	one sha256:8739546433ab1ac72ee93088dce611210effee072b2b586ceac6dde43ebec9ce

	three_file sha1:0e3b14047d3ee365f4f2a1b673db059c3972589c
	three_file sha256:bc4447d50c07497a8bfe6eef817f2364ecca9d471452e43b52756cc1a908bd32

	main sha1:ecf3b3627b498bdcb735cc4343bf165f76964e9a
	main sha256:fff666109892bb4b1c80cd1649d2d8762a0663db8b5d46c8be98360b64fbba5f

	one_tree sha1:22feea448b023a2d864ef94b013735af34d238ba
	one_tree sha256:6e4743f4ef2356b881dda5e91f5c7cdffe870faf350bf7b312f80a20935f5d83

	three sha1:c61a82b60967180544e3c19f819ddbd0c9f89899
	three sha256:0cc6d1eda617ded715170786e31ba4e2d0185404ec5a3508dd0d73b324860c6a

	two sha1:525b7fb068d59950d185a8779dc957c77eed73ba
	two sha256:3b21de3440cd38c2a9e9b464adb923f7054949ed4c918e1a0ac4c95cd52774db

	topic_2 sha1:b4ab76b1a01ea602209932134a44f1e6bd610832
	topic_2 sha256:380ebae0113f877ce46fcdf39d5bc33e4dc0928db5c5a4d5fdc78381c4d55ae3

	two2 sha1:6134ee8f857693b96ff1cc98d3e2fd62b199e5a8
	two2 sha256:87a2d3ee29c83a3dc7afd41c0606b11f67603120b910a7be7840accdc18344d4

	three2 sha1:0567da4d5edd2ff4bb292a465ba9e64dcad9536b
	three2 sha256:cceb3e8eca364fa9a0a39a1efbebecacc664af86cbbd8070571f5faeb5f0e8c3
	EOF

	echo >file original &&
	git add file &&
	git commit -a -m One &&
	git tag tag-one &&
	git tag tag-one-tree HEAD^{tree} &&
	git branch one &&

	echo two >> file &&
	git commit -a -m Two &&
	git tag -a -m "Tag Two" tag-two &&
	git branch two &&

	echo three >> file &&
	git commit -a -m Three &&
	git tag -a -m "Tag Three" tag-three &&
	git tag -a -m "Tag Three file" tag-three-file HEAD^{tree}:file &&
	git branch three &&

	echo main >> file &&
	git commit -a -m Main &&
	git tag -a -m "Tag Main" tag-main &&

	git checkout three &&

	git clone . cloned &&
	cd cloned &&
	git config remote.origin.url ../.git/ &&

	git config remote.config-explicit.url ../.git/ &&
	git config remote.config-explicit.fetch refs/heads/main:remotes/rem/main &&
	git config --add remote.config-explicit.fetch refs/heads/one:remotes/rem/one &&
	git config --add remote.config-explicit.fetch two:remotes/rem/two &&
	git config --add remote.config-explicit.fetch refs/heads/three:remotes/rem/three &&
	remotes="config-explicit" &&

	git config remote.config-glob.url ../.git/ &&
	git config remote.config-glob.fetch refs/heads/*:refs/remotes/rem/* &&
	remotes="$remotes config-glob" &&

	mkdir -p .git/remotes &&
	cat >.git/remotes/remote-explicit <<-\EOF &&
	URL: ../.git/
	Pull: refs/heads/main:remotes/rem/main
	Pull: refs/heads/one:remotes/rem/one
	Pull: two:remotes/rem/two
	Pull: refs/heads/three:remotes/rem/three
	EOF
	remotes="$remotes remote-explicit" &&

	cat >.git/remotes/remote-glob <<-\EOF &&
	URL: ../.git/
	Pull: refs/heads/*:refs/remotes/rem/*
	EOF
	remotes="$remotes remote-glob" &&

	mkdir -p .git/branches &&
	echo "../.git" > .git/branches/branches-default &&
	remotes="$remotes branches-default" &&

	echo "../.git#one" > .git/branches/branches-one &&
	remotes="$remotes branches-one" &&

	for remote in $remotes ; do
		git config branch.br-$remote.remote $remote &&
		git config branch.br-$remote-merge.remote $remote &&
		git config branch.br-$remote-merge.merge refs/heads/three &&
		git config branch.br-$remote-octopus.remote $remote &&
		git config branch.br-$remote-octopus.merge refs/heads/one &&
		git config --add branch.br-$remote-octopus.merge two || return 1
	done &&
	build_script sed_script
'

# Merge logic depends on branch properties and Pull: or .fetch lines
for remote in $remotes ; do
    for branch in "" "-merge" "-octopus" ; do
cat <<EOF
br-$remote$branch
br-$remote$branch $remote
EOF
    done
done > tests

# Merge logic does not depend on branch properties,
# but does depend on Pull: or fetch lines.
# Use two branches completely unrelated from the arguments,
# the clone default and one without branch properties
for branch in main br-unconfig ; do
    echo $branch
    for remote in $remotes ; do
	echo $branch $remote
    done
done >> tests

# Merge logic does not depend on branch properties
# neither in the Pull: or .fetch config
for branch in main br-unconfig ; do
    cat <<EOF
$branch ../.git
$branch ../.git one
$branch ../.git one two
$branch --tags ../.git
$branch ../.git tag tag-one tag tag-three
$branch ../.git tag tag-one-tree tag tag-three-file
$branch ../.git one tag tag-one tag tag-three-file
EOF
done >> tests

while read cmd
do
	case "$cmd" in
	'' | '#'*) continue ;;
	esac
	test=$(echo "$cmd" | sed -e 's|[/ ][/ ]*|_|g')
	pfx=$(printf "%04d" $test_count)
	expect_f="$TEST_DIRECTORY/t5515/fetch.$test"
	actual_f="$pfx-fetch.$test"
	expect_r="$TEST_DIRECTORY/t5515/refs.$test"
	actual_r="$pfx-refs.$test"

	test_expect_success "$cmd" '
		cp "$expect_f" expect_f &&
		convert_expected expect_f sed_script &&
		cp "$expect_r" expect_r &&
		convert_expected expect_r sed_script &&
		{
			echo "# $cmd" &&
			set x $cmd && shift &&
			git symbolic-ref HEAD refs/heads/$1 && shift &&
			rm -f .git/FETCH_HEAD &&
			git for-each-ref \
				refs/heads refs/remotes/rem refs/tags |
			while read val type refname
			do
				git update-ref -d "$refname" "$val" || return 1
			done &&
			git fetch "$@" >/dev/null &&
			cat .git/FETCH_HEAD
		} >"$actual_f" &&
		git show-ref >"$actual_r" &&
		if test -f "expect_f"
		then
			test_cmp "expect_f" "$actual_f" &&
			rm -f "$actual_f"
		else
			# this is to help developing new tests.
			cp "$actual_f" "$expect_f"
			false
		fi &&
		if test -f "expect_r"
		then
			test_cmp "expect_r" "$actual_r" &&
			rm -f "$actual_r"
		else
			# this is to help developing new tests.
			cp "$actual_r" "$expect_r"
			false
		fi
	'
done < tests

test_done
