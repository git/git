#!/bin/sh
#
# Copyright (c) 2007 Santi Béjar, based on t4013 by Junio C Hamano
#
#

test_description='Merge logic in fetch'

# NEEDSWORK: If the overspecification of the expected result is reduced, we
# might be able to run this test in all protocol versions.
GIT_TEST_PROTOCOL_VERSION=

. ./test-lib.sh

LF='
'

test_expect_success setup '
	GIT_AUTHOR_DATE="2006-06-26 00:00:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:00:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

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

	echo master >> file &&
	git commit -a -m Master &&
	git tag -a -m "Tag Master" tag-master &&

	git checkout three &&

	git clone . cloned &&
	cd cloned &&
	git config remote.origin.url ../.git/ &&

	git config remote.config-explicit.url ../.git/ &&
	git config remote.config-explicit.fetch refs/heads/master:remotes/rem/master &&
	git config --add remote.config-explicit.fetch refs/heads/one:remotes/rem/one &&
	git config --add remote.config-explicit.fetch two:remotes/rem/two &&
	git config --add remote.config-explicit.fetch refs/heads/three:remotes/rem/three &&
	remotes="config-explicit" &&

	git config remote.config-glob.url ../.git/ &&
	git config remote.config-glob.fetch refs/heads/*:refs/remotes/rem/* &&
	remotes="$remotes config-glob" &&

	mkdir -p .git/remotes &&
	{
		echo "URL: ../.git/"
		echo "Pull: refs/heads/master:remotes/rem/master"
		echo "Pull: refs/heads/one:remotes/rem/one"
		echo "Pull: two:remotes/rem/two"
		echo "Pull: refs/heads/three:remotes/rem/three"
	} >.git/remotes/remote-explicit &&
	remotes="$remotes remote-explicit" &&

	{
		echo "URL: ../.git/"
		echo "Pull: refs/heads/*:refs/remotes/rem/*"
	} >.git/remotes/remote-glob &&
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
		git config --add branch.br-$remote-octopus.merge two
	done
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
for branch in master br-unconfig ; do
    echo $branch
    for remote in $remotes ; do
	echo $branch $remote
    done
done >> tests

# Merge logic does not depend on branch properties
# neither in the Pull: or .fetch config
for branch in master br-unconfig ; do
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
		{
			echo "# $cmd"
			set x $cmd; shift
			git symbolic-ref HEAD refs/heads/$1 ; shift
			rm -f .git/FETCH_HEAD
			git for-each-ref \
				refs/heads refs/remotes/rem refs/tags |
			while read val type refname
			do
				git update-ref -d "$refname" "$val"
			done
			git fetch "$@" >/dev/null
			cat .git/FETCH_HEAD
		} >"$actual_f" &&
		git show-ref >"$actual_r" &&
		if test -f "$expect_f"
		then
			test_cmp "$expect_f" "$actual_f" &&
			rm -f "$actual_f"
		else
			# this is to help developing new tests.
			cp "$actual_f" "$expect_f"
			false
		fi &&
		if test -f "$expect_r"
		then
			test_cmp "$expect_r" "$actual_r" &&
			rm -f "$actual_r"
		else
			# this is to help developing new tests.
			cp "$actual_r" "$expect_r"
			false
		fi
	'
done < tests

test_done
