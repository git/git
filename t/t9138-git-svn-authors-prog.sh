#!/bin/sh
#
# Copyright (c) 2009 Eric Wong, Mark Lodato
#

test_description='git svn authors prog tests'

. ./lib-git-svn.sh

cat > svn-authors-prog <<'EOF'
#!/usr/bin/perl
$_ = shift;
if (s/-sub$//)  {
	print "$_ <$_\@sub.example.com>\n";
}
else {
	print "$_ <$_\@example.com>\n";
}
EOF
chmod +x svn-authors-prog

cat > svn-authors <<'EOF'
ff = FFFFFFF FFFFFFF <fFf@other.example.com>
EOF

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
		cd x
		test "`git rev-list refs/remotes/git-svn | wc -l`" -eq 6
	)
	'

test_expect_success 'authors-prog ran correctly' '
	(
		cd x
		git rev-list -1 --pretty=raw refs/remotes/git-svn~1 | \
		  grep "^author ee-foo <ee-foo@example\.com> " &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~2 | \
		  grep "^author dd <dd@sub\.example\.com> " &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~3 | \
		  grep "^author cc <cc@sub\.example\.com> " &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~4 | \
		  grep "^author bb <bb@example\.com> " &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn~5 | \
		  grep "^author aa <aa@example\.com> "
	)
	'

test_expect_success 'authors-file overrode authors-prog' '
	(
		cd x
		git rev-list -1 --pretty=raw refs/remotes/git-svn | \
		  grep "^author FFFFFFF FFFFFFF <fFf@other\.example\.com> "
	)
	'

git --git-dir=x/.git config --unset svn.authorsfile
git --git-dir=x/.git config --unset svn.authorsprog

test_expect_success 'authors-prog handled special characters in username' '
	svn mkdir -m bad --username "xyz; touch evil" "$svnrepo"/bad &&
	(
		cd x &&
		git svn --authors-prog=../svn-authors-prog fetch &&
		git rev-list -1 --pretty=raw refs/remotes/git-svn |
		grep "^author xyz; touch evil <xyz; touch evil@example\.com> " &&
		! test -f evil
	)
'

test_done
