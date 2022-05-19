#!/bin/sh

test_description='test refspec written by clone-command'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	# Make two branches, "main" and "side"
	echo one >file &&
	but add file &&
	but cummit -m one &&
	echo two >file &&
	but cummit -a -m two &&
	but tag two &&
	echo three >file &&
	but cummit -a -m three &&
	but checkout -b side &&
	echo four >file &&
	but cummit -a -m four &&
	but checkout main &&
	but tag five &&

	# default clone
	but clone . dir_all &&

	# default clone --no-tags
	but clone --no-tags . dir_all_no_tags &&

	# default --single that follows HEAD=main
	but clone --single-branch . dir_main &&

	# default --single that follows HEAD=main with no tags
	but clone --single-branch --no-tags . dir_main_no_tags &&

	# default --single that follows HEAD=side
	but checkout side &&
	but clone --single-branch . dir_side &&

	# explicit --single that follows side
	but checkout main &&
	but clone --single-branch --branch side . dir_side2 &&

	# default --single with --mirror
	but clone --single-branch --mirror . dir_mirror &&

	# default --single with --branch and --mirror
	but clone --single-branch --mirror --branch side . dir_mirror_side &&

	# --single that does not know what branch to follow
	but checkout two^ &&
	but clone --single-branch . dir_detached &&

	# explicit --single with tag
	but clone --single-branch --branch two . dir_tag &&

	# explicit --single with tag and --no-tags
	but clone --single-branch --no-tags --branch two . dir_tag_no_tags &&

	# advance both "main" and "side" branches
	but checkout side &&
	echo five >file &&
	but cummit -a -m five &&
	but checkout main &&
	echo six >file &&
	but cummit -a -m six &&

	# update tag
	but tag -d two && but tag two
'

test_expect_success 'by default all branches will be kept updated' '
	(
		cd dir_all &&
		but fetch &&
		but for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# follow both main and side
	but for-each-ref refs/heads >expect &&
	test_cmp expect actual
'

test_expect_success 'by default no tags will be kept updated' '
	(
		cd dir_all &&
		but fetch &&
		but for-each-ref refs/tags >../actual
	) &&
	but for-each-ref refs/tags >expect &&
	! test_cmp expect actual &&
	test_line_count = 2 actual
'

test_expect_success 'clone with --no-tags' '
	(
		cd dir_all_no_tags &&
		grep tagOpt .but/config &&
		but fetch &&
		but for-each-ref refs/tags >../actual
	) &&
	test_must_be_empty actual
'

test_expect_success '--single-branch while HEAD pointing at main' '
	(
		cd dir_main &&
		but fetch --force &&
		but for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# only follow main
	but for-each-ref refs/heads/main >expect &&
	# get & check latest tags
	test_cmp expect actual &&
	(
		cd dir_main &&
		but fetch --tags --force &&
		but for-each-ref refs/tags >../actual
	) &&
	but for-each-ref refs/tags >expect &&
	test_cmp expect actual &&
	test_line_count = 2 actual
'

test_expect_success '--single-branch while HEAD pointing at main and --no-tags' '
	(
		cd dir_main_no_tags &&
		but fetch &&
		but for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# only follow main
	but for-each-ref refs/heads/main >expect &&
	test_cmp expect actual &&
	# get tags (noop)
	(
		cd dir_main_no_tags &&
		but fetch &&
		but for-each-ref refs/tags >../actual
	) &&
	test_must_be_empty actual &&
	test_line_count = 0 actual &&
	# get tags with --tags overrides tagOpt
	(
		cd dir_main_no_tags &&
		but fetch --tags &&
		but for-each-ref refs/tags >../actual
	) &&
	but for-each-ref refs/tags >expect &&
	test_cmp expect actual &&
	test_line_count = 2 actual
'

test_expect_success '--single-branch while HEAD pointing at side' '
	(
		cd dir_side &&
		but fetch &&
		but for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# only follow side
	but for-each-ref refs/heads/side >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch side' '
	(
		cd dir_side2 &&
		but fetch &&
		but for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# only follow side
	but for-each-ref refs/heads/side >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch with tag fetches updated tag' '
	(
		cd dir_tag &&
		but fetch &&
		but for-each-ref refs/tags >../actual
	) &&
	but for-each-ref refs/tags >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch with tag fetches updated tag despite --no-tags' '
	(
		cd dir_tag_no_tags &&
		but fetch &&
		but for-each-ref refs/tags >../actual
	) &&
	but for-each-ref refs/tags/two >expect &&
	test_cmp expect actual &&
	test_line_count = 1 actual
'

test_expect_success '--single-branch with --mirror' '
	(
		cd dir_mirror &&
		but fetch &&
		but for-each-ref refs > ../actual
	) &&
	but for-each-ref refs >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with explicit --branch and --mirror' '
	(
		cd dir_mirror_side &&
		but fetch &&
		but for-each-ref refs > ../actual
	) &&
	but for-each-ref refs >expect &&
	test_cmp expect actual
'

test_expect_success '--single-branch with detached' '
	(
		cd dir_detached &&
		but fetch &&
		but for-each-ref refs/remotes/origin >refs &&
		sed -e "/HEAD$/d" \
		    -e "s|/remotes/origin/|/heads/|" refs >../actual
	) &&
	# nothing
	test_must_be_empty actual
'

test_done
