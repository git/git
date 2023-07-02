#!/bin/sh

test_description='test refspec written by clone-command'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	# Make two branches, "main" and "side"
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
	git checkout main &&
	git tag five &&

	# default clone
	git clone . dir_all &&

	# default clone --no-tags
	git clone --no-tags . dir_all_no_tags &&

	# default --single that follows HEAD=main
	git clone --single-branch . dir_main &&

	# default --single that follows HEAD=main with no tags
	git clone --single-branch --no-tags . dir_main_no_tags &&

	# default --single that follows HEAD=side
	git checkout side &&
	git clone --single-branch . dir_side &&

	# explicit --single that follows side
	git checkout main &&
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

	# explicit --single with tag and --no-tags
	git clone --single-branch --no-tags --branch two . dir_tag_no_tags &&

	# advance both "main" and "side" branches
	git checkout side &&
	echo five >file &&
	git commit -a -m five &&
	git checkout main &&
	echo six >file &&
	git commit -a -m six &&

	# update tag
	git tag -d two && git tag two
'

test_expect_success 'by default all branches will be kept updated' '
	(
		cd dir_all &&
		git fetch &&
		git for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# follow both main and side
	git for-each-ref refs/heads >expect &&
	test_cmp expect actual
'

test_expect_success 'by default no tags will be kept updated' '
	(
		cd dir_all &&
		git fetch &&
		git for-each-ref refs/tags >../actual
	) &&
	git for-each-ref refs/tags >expect &&
	! test_cmp expect actual &&
	test_line_count = 2 actual
'

test_expect_success 'clone with --no-tags' '
	(
		cd dir_all_no_tags &&
		grep tagOpt .git/config &&
		git fetch &&
		git for-each-ref refs/tags >../actual
	) &&
	test_must_be_empty actual
'

test_expect_success '--single-branch while HEAD pointing at main' '
	(
		cd dir_main &&
		git fetch --force &&
		git for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# only follow main
	git for-each-ref refs/heads/main >expect &&
	# get & check latest tags
	test_cmp expect actual &&
	(
		cd dir_main &&
		git fetch --tags --force &&
		git for-each-ref refs/tags >../actual
	) &&
	git for-each-ref refs/tags >expect &&
	test_cmp expect actual &&
	test_line_count = 2 actual
'

test_expect_success '--single-branch while HEAD pointing at main and --no-tags' '
	(
		cd dir_main_no_tags &&
		git fetch &&
		git for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# only follow main
	git for-each-ref refs/heads/main >expect &&
	test_cmp expect actual &&
	# get tags (noop)
	(
		cd dir_main_no_tags &&
		git fetch &&
		git for-each-ref refs/tags >../actual
	) &&
	test_must_be_empty actual &&
	test_line_count = 0 actual &&
	# get tags with --tags overrides tagOpt
	(
		cd dir_main_no_tags &&
		git fetch --tags &&
		git for-each-ref refs/tags >../actual
	) &&
	git for-each-ref refs/tags >expect &&
	test_cmp expect actual &&
	test_line_count = 2 actual
'

test_expect_success '--single-branch while HEAD pointing at side' '
	(
		cd dir_side &&
		git fetch &&
		git for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# only follow side
	git for-each-ref refs/heads/side >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch side' '
	(
		cd dir_side2 &&
		git fetch &&
		git for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# only follow side
	git for-each-ref refs/heads/side >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch with tag fetches updated tag' '
	(
		cd dir_tag &&
		git fetch &&
		git for-each-ref refs/tags >../actual
	) &&
	git for-each-ref refs/tags >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch with tag fetches updated tag despite --no-tags' '
	(
		cd dir_tag_no_tags &&
		git fetch &&
		git for-each-ref refs/tags >../actual
	) &&
	git for-each-ref refs/tags/two >expect &&
	test_cmp expect actual &&
	test_line_count = 1 actual
'

test_expect_success '--single-branch with --mirror' '
	(
		cd dir_mirror &&
		git fetch &&
		git for-each-ref refs > ../actual
	) &&
	git for-each-ref refs >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch and --mirror' '
	(
		cd dir_mirror_side &&
		git fetch &&
		git for-each-ref refs > ../actual
	) &&
	git for-each-ref refs >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with detached' '
	(
		cd dir_detached &&
		git fetch &&
		git for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# nothing
	test_must_be_empty actual
'

test_done
