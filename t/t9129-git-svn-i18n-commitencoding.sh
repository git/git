#!/bin/sh
#
# Copyright (c) 2008 Eric Wong

test_description='git svn honors i18n.commitEncoding in config'

. ./lib-git-svn.sh

compare_git_head_with () {
	nr=$(wc -l < "$1")
	a=7
	b=$(($a + $nr - 1))
	git cat-file commit HEAD | sed -ne "$a,${b}p" >current &&
	test_cmp current "$1"
}

prepare_utf8_locale

compare_svn_head_with () {
	# extract just the log message and strip out committer info.
	# don't use --limit here since svn 1.1.x doesn't have it,
	LC_ALL="$GIT_TEST_UTF8_LOCALE" svn log $(git svn info --url) | perl -w -e '
		use bytes;
		$/ = ("-"x72) . "\n";
		my @x = <STDIN>;
		@x = split(/\n/, $x[1]);
		splice(@x, 0, 2);
		$x[-1] = "";
		print join("\n", @x);
	' > current &&
	test_cmp current "$1"
}

for H in ISO8859-1 eucJP ISO-2022-JP
do
	test_expect_success "$H setup" '
		mkdir $H &&
		svn_cmd import -m "$H test" $H "$svnrepo"/$H &&
		git svn clone "$svnrepo"/$H $H
	'
done

for H in ISO8859-1 eucJP ISO-2022-JP
do
	test_expect_success "$H commit on git side" '
	(
		cd $H &&
		git config i18n.commitencoding $H &&
		git checkout -b t refs/remotes/git-svn &&
		echo $H >F &&
		git add F &&
		git commit -a -F "$TEST_DIRECTORY"/t3900/$H.txt &&
		E=$(git cat-file commit HEAD | sed -ne "s/^encoding //p") &&
		test "z$E" = "z$H" &&
		compare_git_head_with "$TEST_DIRECTORY"/t3900/$H.txt
	)
	'
done

for H in ISO8859-1 eucJP ISO-2022-JP
do
	test_expect_success "$H dcommit to svn" '
	(
		cd $H &&
		git svn dcommit &&
		git cat-file commit HEAD | grep git-svn-id: &&
		E=$(git cat-file commit HEAD | sed -ne "s/^encoding //p") &&
		test "z$E" = "z$H" &&
		compare_git_head_with "$TEST_DIRECTORY"/t3900/$H.txt
	)
	'
done

test_expect_success UTF8 'ISO-8859-1 should match UTF-8 in svn' '
	(
		cd ISO8859-1 &&
		compare_svn_head_with "$TEST_DIRECTORY"/t3900/1-UTF-8.txt
	)
'

for H in eucJP ISO-2022-JP
do
	test_expect_success UTF8 "$H should match UTF-8 in svn" '
		(
			cd $H &&
			compare_svn_head_with "$TEST_DIRECTORY"/t3900/2-UTF-8.txt
		)
	'
done

test_done
