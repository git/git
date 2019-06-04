#!/bin/sh
#
# Copyright (c) 2009 Eric Wong, Mark Lodato
#

test_description='git svn authors prog tests'

. ./lib-git-svn.sh

write_script svn-authors-prog "$PERL_PATH" <<-\EOF
	$_ = shift;
	if (s/-hermit//) {
		print "$_ <>\n";
	} elsif (s/-sub$//)  {
		print "$_ <$_\@sub.example.com>\n";
	} else {
		print "$_ <$_\@example.com>\n";
	}
EOF

test_expect_success 'svn-authors setup' '
	cat >svn-authors <<-\EOF
	ff = FFFFFFF FFFFFFF <fFf@other.example.com>
	EOF
'

test_expect_success 'setup svnrepo' '
	for i in aa bb cc-sub dd-sub ee-foo ff
	do
		svn mkdir -m $i --username $i "$svnrepo"/$i
	done
'

test_expect_success 'import authors with prog and file' '
	git svn clone --authors-prog=./svn-authors-prog \
	    --authors-file=svn-authors "$svnrepo" x
'

test_expect_success 'imported 6 revisions successfully' '
	(
		cd x &&
		git rev-list refs/remotes/git-svn >actual &&
		test_line_count = 6 actual
	)
'

test_expect_success 'authors-prog ran correctly' '
	(
		cd x &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~1 >actual &&
		grep "^author ee-foo <ee-foo@example\.com> " actual &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~2 >actual &&
		grep "^author dd <dd@sub\.example\.com> " actual &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~3 >actual &&
		grep "^author cc <cc@sub\.example\.com> " actual &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~4 >actual &&
		grep "^author bb <bb@example\.com> " actual &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~5 >actual &&
		grep "^author aa <aa@example\.com> " actual
	)
'

test_expect_success 'authors-file overrode authors-prog' '
	(
		cd x &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn >actual &&
		grep "^author FFFFFFF FFFFFFF <fFf@other\.example\.com> " actual
	)
'

git --git-dir=x/.git config --unset svn.authorsfile
git --git-dir=x/.git config --unset svn.authorsprog

test_expect_success 'authors-prog imported user without email' '
	svn mkdir -m gg --username gg-hermit "$svnrepo"/gg &&
	(
		cd x &&
		git svn fetch --authors-prog=../svn-authors-prog &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn | \
		  grep "^author gg <> "
	)
'

test_expect_success 'imported without authors-prog and authors-file' '
	svn mkdir -m hh --username hh "$svnrepo"/hh &&
	(
		uuid=$(svn info "$svnrepo" |
			sed -n "s/^Repository UUID: //p") &&
		cd x &&
		git svn fetch &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn | \
		  grep "^author hh <hh@$uuid> "
	)
'

test_expect_success 'authors-prog handled special characters in username' '
	svn mkdir -m bad --username "xyz; touch evil" "$svnrepo"/bad &&
	(
		cd x &&
		git svn --authors-prog=../svn-authors-prog fetch &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn >actual &&
		grep "^author xyz; touch evil <xyz; touch evil@example\.com> " actual &&
		! test -f evil
	)
'

test_done
