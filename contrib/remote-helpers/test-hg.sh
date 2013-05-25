#!/bin/sh
#
# Copyright (c) 2012 Felipe Contreras
#
# Base commands from hg-git tests:
# https://bitbucket.org/durin42/hg-git/src
#

test_description='Test remote-hg'

. ./test-lib.sh

if ! test_have_prereq PYTHON; then
	skip_all='skipping remote-hg tests; python not available'
	test_done
fi

if ! python -c 'import mercurial'; then
	skip_all='skipping remote-hg tests; mercurial not available'
	test_done
fi

check () {
	echo $3 > expected &&
	git --git-dir=$1/.git log --format='%s' -1 $2 > actual
	test_cmp expected actual
}

check_branch () {
	echo $3 > expected &&
	hg -R $1 log -r $2 --template '{desc}\n' > actual &&
	test_cmp expected actual
}

check_bookmark () {
	echo $3 > expected &&
	hg -R $1 log -r "bookmark('$2')" --template '{desc}\n' > actual &&
	test_cmp expected actual
}

setup () {
	(
	echo "[ui]"
	echo "username = H G Wells <wells@example.com>"
	echo "[extensions]"
	echo "mq ="
	) >> "$HOME"/.hgrc &&

	GIT_AUTHOR_DATE="2007-01-01 00:00:00 +0230" &&
	GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE" &&
	export GIT_COMMITTER_DATE GIT_AUTHOR_DATE
}

setup

test_expect_success 'cloning' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	hg init hgrepo &&
	cd hgrepo &&
	echo zero > content &&
	hg add content &&
	hg commit -m zero
	) &&

	git clone "hg::hgrepo" gitrepo &&
	check gitrepo HEAD zero
'

test_expect_success 'cloning with branches' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	cd hgrepo &&
	hg branch next &&
	echo next > content &&
	hg commit -m next
	) &&

	git clone "hg::hgrepo" gitrepo &&
	check gitrepo origin/branches/next next
'

test_expect_success 'cloning with bookmarks' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	cd hgrepo &&
	hg checkout default &&
	hg bookmark feature-a &&
	echo feature-a > content &&
	hg commit -m feature-a
	) &&

	git clone "hg::hgrepo" gitrepo &&
	check gitrepo origin/feature-a feature-a
'

test_expect_success 'update bookmark' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	cd hgrepo &&
	hg bookmark devel
	) &&

	(
	git clone "hg::hgrepo" gitrepo &&
	cd gitrepo &&
	git checkout --quiet devel &&
	echo devel > content &&
	git commit -a -m devel &&
	git push --quiet
	) &&

	check_bookmark hgrepo devel devel
'

test_expect_success 'new bookmark' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	git clone "hg::hgrepo" gitrepo &&
	cd gitrepo &&
	git checkout --quiet -b feature-b &&
	echo feature-b > content &&
	git commit -a -m feature-b &&
	git push --quiet origin feature-b
	) &&

	check_bookmark hgrepo feature-b feature-b
'

# cleanup previous stuff
rm -rf hgrepo

author_test () {
	echo $1 >> content &&
	hg commit -u "$2" -m "add $1" &&
	echo "$3" >> ../expected
}

test_expect_success 'authors' '
	test_when_finished "rm -rf hgrepo gitrepo" &&

	(
	hg init hgrepo &&
	cd hgrepo &&

	touch content &&
	hg add content &&

	> ../expected &&
	author_test alpha "" "H G Wells <wells@example.com>" &&
	author_test beta "test" "test <unknown>" &&
	author_test beta "test <test@example.com> (comment)" "test <test@example.com>" &&
	author_test gamma "<test@example.com>" "Unknown <test@example.com>" &&
	author_test delta "name<test@example.com>" "name <test@example.com>" &&
	author_test epsilon "name <test@example.com" "name <test@example.com>" &&
	author_test zeta " test " "test <unknown>" &&
	author_test eta "test < test@example.com >" "test <test@example.com>" &&
	author_test theta "test >test@example.com>" "test <test@example.com>" &&
	author_test iota "test < test <at> example <dot> com>" "test <unknown>" &&
	author_test kappa "test@example.com" "Unknown <test@example.com>"
	) &&

	git clone "hg::hgrepo" gitrepo &&
	git --git-dir=gitrepo/.git log --reverse --format="%an <%ae>" > actual &&

	test_cmp expected actual
'

test_expect_success 'strip' '
	test_when_finished "rm -rf hgrepo gitrepo" &&

	(
	hg init hgrepo &&
	cd hgrepo &&

	echo one >> content &&
	hg add content &&
	hg commit -m one &&

	echo two >> content &&
	hg commit -m two
	) &&

	git clone "hg::hgrepo" gitrepo &&

	(
	cd hgrepo &&
	hg strip 1 &&

	echo three >> content &&
	hg commit -m three &&

	echo four >> content &&
	hg commit -m four
	) &&

	(
	cd gitrepo &&
	git fetch &&
	git log --format="%s" origin/master > ../actual
	) &&

	hg -R hgrepo log --template "{desc}\n" > expected &&
	test_cmp actual expected
'

test_expect_success 'remote push with master bookmark' '
	test_when_finished "rm -rf hgrepo gitrepo*" &&

	(
	hg init hgrepo &&
	cd hgrepo &&
	echo zero > content &&
	hg add content &&
	hg commit -m zero &&
	hg bookmark master &&
	echo one > content &&
	hg commit -m one
	) &&

	(
	git clone "hg::hgrepo" gitrepo &&
	cd gitrepo &&
	echo two > content &&
	git commit -a -m two &&
	git push
	) &&

	check_branch hgrepo default two
'

cat > expected <<EOF
changeset:   0:6e2126489d3d
tag:         tip
user:        A U Thor <author@example.com>
date:        Mon Jan 01 00:00:00 2007 +0230
summary:     one

EOF

test_expect_success 'remote push from master branch' '
	test_when_finished "rm -rf hgrepo gitrepo*" &&

	hg init hgrepo &&

	(
	git init gitrepo &&
	cd gitrepo &&
	git remote add origin "hg::../hgrepo" &&
	echo one > content &&
	git add content &&
	git commit -a -m one &&
	git push origin master
	) &&

	hg -R hgrepo log > actual &&
	cat actual &&
	test_cmp expected actual &&

	check_branch hgrepo default one
'

GIT_REMOTE_HG_TEST_REMOTE=1
export GIT_REMOTE_HG_TEST_REMOTE

test_expect_success 'remote cloning' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	hg init hgrepo &&
	cd hgrepo &&
	echo zero > content &&
	hg add content &&
	hg commit -m zero
	) &&

	git clone "hg::hgrepo" gitrepo &&
	check gitrepo HEAD zero
'

test_expect_success 'remote update bookmark' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	cd hgrepo &&
	hg bookmark devel
	) &&

	(
	git clone "hg::hgrepo" gitrepo &&
	cd gitrepo &&
	git checkout --quiet devel &&
	echo devel > content &&
	git commit -a -m devel &&
	git push --quiet
	) &&

	check_bookmark hgrepo devel devel
'

test_expect_success 'remote new bookmark' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	git clone "hg::hgrepo" gitrepo &&
	cd gitrepo &&
	git checkout --quiet -b feature-b &&
	echo feature-b > content &&
	git commit -a -m feature-b &&
	git push --quiet origin feature-b
	) &&

	check_bookmark hgrepo feature-b feature-b
'

test_expect_failure 'remote push diverged' '
	test_when_finished "rm -rf gitrepo*" &&

	git clone "hg::hgrepo" gitrepo &&

	(
	cd hgrepo &&
	hg checkout default &&
	echo bump > content &&
	hg commit -m bump
	) &&

	(
	cd gitrepo &&
	echo diverge > content &&
	git commit -a -m diverged &&
	test_expect_code 1 git push 2> error &&
	grep "^ ! \[rejected\] *master -> master (non-fast-forward)$" error
	) &&

	check_branch hgrepo default bump
'

test_done
