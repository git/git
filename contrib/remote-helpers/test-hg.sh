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
	(
	cd $1 &&
	git log --format='%s' -1 &&
	git symbolic-ref HEAD
	) > actual &&
	(
	echo $2 &&
	echo "refs/heads/$3"
	) > expected &&
	test_cmp expected actual
}

setup () {
	(
	echo "[ui]"
	echo "username = H G Wells <wells@example.com>"
	echo "[extensions]"
	echo "mq ="
	) >> "$HOME"/.hgrc
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
	check gitrepo zero master
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
	check gitrepo next next &&

	(cd hgrepo && hg checkout default) &&

	git clone "hg::hgrepo" gitrepo2 &&
	check gitrepo2 zero master
'

test_expect_success 'cloning with bookmarks' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	cd hgrepo &&
	hg bookmark feature-a &&
	echo feature-a > content &&
	hg commit -m feature-a
	) &&

	git clone "hg::hgrepo" gitrepo &&
	check gitrepo feature-a feature-a
'

test_expect_success 'cloning with detached head' '
	test_when_finished "rm -rf gitrepo*" &&

	(
	cd hgrepo &&
	hg update -r 0
	) &&

	git clone "hg::hgrepo" gitrepo &&
	check gitrepo zero master
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

	hg -R hgrepo bookmarks | egrep "devel[	 ]+3:"
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

test_done
