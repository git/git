#!/bin/sh

test_description='test refspec written by clone-command'
. ./test-lib.sh

test_expect_success 'setup' '
	# Make two branches, "master" and "side"
	echo one >file &&
	git add file &&
	git commit -m one &&
	echo two >file &&
	git commit -a -m two &&
	git tag two &&
	echo three >file &&
	git commit -a -m three &&
	git checkout -b side &&
	echo four >file &&
	git commit -a -m four &&
	git checkout master &&

	# default clone
	git clone . dir_all &&

	# default --single that follows HEAD=master
	git clone --single-branch . dir_master &&

	# default --single that follows HEAD=side
	git checkout side &&
	git clone --single-branch . dir_side &&

	# explicit --single that follows side
	git checkout master &&
	git clone --single-branch --branch side . dir_side2 &&

	# default --single with --mirror
	git clone --single-branch --mirror . dir_mirror &&

	# default --single with --branch and --mirror
	git clone --single-branch --mirror --branch side . dir_mirror_side &&

	# --single that does not know what branch to follow
	git checkout two^ &&
	git clone --single-branch . dir_detached &&

	# explicit --single with tag
	git clone --single-branch --branch two . dir_tag &&

	# advance both "master" and "side" branches
	git checkout side &&
	echo five >file &&
	git commit -a -m five &&
	git checkout master &&
	echo six >file &&
	git commit -a -m six &&

	# update tag
	git tag -d two && git tag two
'

test_expect_success 'by default all branches will be kept updated' '
	(
		cd dir_all && git fetch &&
		git for-each-ref refs/remotes/origin |
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" >../actual
	) &&
	# follow both master and side
	git for-each-ref refs/heads >expect &&
	test_cmp expect actual
'

test_expect_success 'by default no tags will be kept updated' '
	(
		cd dir_all && git fetch &&
		git for-each-ref refs/tags >../actual
	) &&
	git for-each-ref refs/tags >expect &&
	test_must_fail test_cmp expect actual
'

test_expect_success '--single-branch while HEAD pointing at master' '
	(
		cd dir_master && git fetch &&
		git for-each-ref refs/remotes/origin |
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" >../actual
	) &&
	# only follow master
	git for-each-ref refs/heads/master >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch while HEAD pointing at side' '
	(
		cd dir_side && git fetch &&
		git for-each-ref refs/remotes/origin |
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" >../actual
	) &&
	# only follow side
	git for-each-ref refs/heads/side >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch side' '
	(
		cd dir_side2 && git fetch &&
		git for-each-ref refs/remotes/origin |
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" >../actual
	) &&
	# only follow side
	git for-each-ref refs/heads/side >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch with tag fetches updated tag' '
	(
		cd dir_tag && git fetch &&
		git for-each-ref refs/tags >../actual
	) &&
	git for-each-ref refs/tags >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with --mirror' '
	(
		cd dir_mirror && git fetch &&
		git for-each-ref refs > ../actual
	) &&
	git for-each-ref refs >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch and --mirror' '
	(
		cd dir_mirror_side && git fetch &&
		git for-each-ref refs > ../actual
	) &&
	git for-each-ref refs >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with detached' '
	(
		cd dir_detached && git fetch &&
		git for-each-ref refs/remotes/origin |
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" >../actual
	)
	# nothing
	>expect &&
	test_cmp expect actual
'

test_done
