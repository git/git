#!/bin/sh

test_description='split index mode tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# We need total control of index splitting here
sane_unset GIT_TEST_SPLIT_INDEX

# Testing a hard coded SHA against an index with an extension
# that can vary from run to run is problematic so we disable
# those extensions.
sane_unset GIT_TEST_FSMONITOR
sane_unset GIT_TEST_INDEX_THREADS

# Create a file named as $1 with content read from stdin.
# Set the file's mtime to a few seconds in the past to avoid racy situations.
create_non_racy_file () {
	cat >"$1" &&
	test-tool chmtime =-5 "$1"
}

test_expect_success 'setup' '
	test_oid_cache <<-EOF
	own_v3 sha1:8299b0bcd1ac364e5f1d7768efb62fa2da79a339
	own_v3 sha256:38a6d2925e3eceec33ad7b34cbff4e0086caa0daf28f31e51f5bd94b4a7af86b

	base_v3 sha1:39d890139ee5356c7ef572216cebcd27aa41f9df
	base_v3 sha256:c9baeadf905112bf6c17aefbd7d02267afd70ded613c30cafed2d40cb506e1ed

	own_v4 sha1:432ef4b63f32193984f339431fd50ca796493569
	own_v4 sha256:6738ac6319c25b694afa7bcc313deb182d1a59b68bf7a47b4296de83478c0420

	base_v4 sha1:508851a7f0dfa8691e9f69c7f055865389012491
	base_v4 sha256:3177d4adfdd4b6904f7e921d91d715a471c0dde7cf6a4bba574927f02b699508
	EOF
'

test_expect_success 'enable split index' '
	git config splitIndex.maxPercentChange 100 &&
	git update-index --split-index &&
	test-tool dump-split-index .git/index >actual &&
	indexversion=$(test-tool index-version <.git/index) &&

	# NEEDSWORK: Stop hard-coding checksums.
	if test "$indexversion" = "4"
	then
		own=$(test_oid own_v4) &&
		base=$(test_oid base_v4)
	else
		own=$(test_oid own_v3) &&
		base=$(test_oid base_v3)
	fi &&

	cat >expect <<-EOF &&
	own $own
	base $base
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'add one file' '
	create_non_racy_file one &&
	git update-index --add one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	base $base
	100644 $EMPTY_BLOB 0	one
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'disable split index' '
	git update-index --no-split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	BASE=$(test-tool dump-split-index .git/index | sed -n "s/^own/base/p") &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	not a split index
	EOF
	test_cmp expect actual
'

test_expect_success 'enable split index again, "one" now belongs to base index"' '
	git update-index --split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'modify original file, base index untouched' '
	echo modified | create_non_racy_file one &&
	file1_blob=$(git hash-object one) &&
	git update-index one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $file1_blob 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<-EOF &&
	$BASE
	100644 $file1_blob 0Q
	replacements: 0
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'add another file, which stays index' '
	create_non_racy_file two &&
	git update-index --add two &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $file1_blob 0	one
	100644 $EMPTY_BLOB 0	two
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<-EOF &&
	$BASE
	100644 $file1_blob 0Q
	100644 $EMPTY_BLOB 0	two
	replacements: 0
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'remove file not in base index' '
	git update-index --force-remove two &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $file1_blob 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	q_to_tab >expect <<-EOF &&
	$BASE
	100644 $file1_blob 0Q
	replacements: 0
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'remove file in base index' '
	git update-index --force-remove one &&
	git ls-files --stage >ls-files.actual &&
	test_must_be_empty ls-files.actual &&

	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions: 0
	EOF
	test_cmp expect actual
'

test_expect_success 'add original file back' '
	create_non_racy_file one &&
	git update-index --add one &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	100644 $EMPTY_BLOB 0	one
	replacements:
	deletions: 0
	EOF
	test_cmp expect actual
'

test_expect_success 'add new file' '
	create_non_racy_file two &&
	git update-index --add two &&
	git ls-files --stage >actual &&
	cat >expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	100644 $EMPTY_BLOB 0	two
	EOF
	test_cmp expect actual
'

test_expect_success 'unify index, two files remain' '
	git update-index --no-split-index &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	100644 $EMPTY_BLOB 0	two
	EOF
	test_cmp ls-files.expect ls-files.actual &&

	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	not a split index
	EOF
	test_cmp expect actual
'

test_expect_success 'rev-parse --shared-index-path' '
	test_create_repo split-index &&
	(
		cd split-index &&
		git update-index --split-index &&
		echo .git/sharedindex* >expect &&
		git rev-parse --shared-index-path >actual &&
		test_cmp expect actual &&
		mkdir subdirectory &&
		cd subdirectory &&
		echo ../.git/sharedindex* >expect &&
		git rev-parse --shared-index-path >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'set core.splitIndex config variable to true' '
	git config core.splitIndex true &&
	create_non_racy_file three &&
	git update-index --add three &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	100644 $EMPTY_BLOB 0	three
	100644 $EMPTY_BLOB 0	two
	EOF
	test_cmp ls-files.expect ls-files.actual &&
	BASE=$(test-tool dump-split-index .git/index | grep "^base") &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'set core.splitIndex config variable to false' '
	git config core.splitIndex false &&
	git update-index --force-remove three &&
	git ls-files --stage >ls-files.actual &&
	cat >ls-files.expect <<-EOF &&
	100644 $EMPTY_BLOB 0	one
	100644 $EMPTY_BLOB 0	two
	EOF
	test_cmp ls-files.expect ls-files.actual &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	not a split index
	EOF
	test_cmp expect actual
'

test_expect_success 'set core.splitIndex config variable back to true' '
	git config core.splitIndex true &&
	create_non_racy_file three &&
	git update-index --add three &&
	BASE=$(test-tool dump-split-index .git/index | grep "^base") &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual &&
	create_non_racy_file four &&
	git update-index --add four &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	100644 $EMPTY_BLOB 0	four
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'check behavior with splitIndex.maxPercentChange unset' '
	git config --unset splitIndex.maxPercentChange &&
	create_non_racy_file five &&
	git update-index --add five &&
	BASE=$(test-tool dump-split-index .git/index | grep "^base") &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual &&
	create_non_racy_file six &&
	git update-index --add six &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	100644 $EMPTY_BLOB 0	six
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'check splitIndex.maxPercentChange set to 0' '
	git config splitIndex.maxPercentChange 0 &&
	create_non_racy_file seven &&
	git update-index --add seven &&
	BASE=$(test-tool dump-split-index .git/index | grep "^base") &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual &&
	create_non_racy_file eight &&
	git update-index --add eight &&
	BASE=$(test-tool dump-split-index .git/index | grep "^base") &&
	test-tool dump-split-index .git/index | sed "/^own/d" >actual &&
	cat >expect <<-EOF &&
	$BASE
	replacements:
	deletions:
	EOF
	test_cmp expect actual
'

test_expect_success 'shared index files expire after 2 weeks by default' '
	create_non_racy_file ten &&
	git update-index --add ten &&
	test $(ls .git/sharedindex.* | wc -l) -gt 2 &&
	just_under_2_weeks_ago=$((5-14*86400)) &&
	test-tool chmtime =$just_under_2_weeks_ago .git/sharedindex.* &&
	create_non_racy_file eleven &&
	git update-index --add eleven &&
	test $(ls .git/sharedindex.* | wc -l) -gt 2 &&
	just_over_2_weeks_ago=$((-1-14*86400)) &&
	test-tool chmtime =$just_over_2_weeks_ago .git/sharedindex.* &&
	create_non_racy_file twelve &&
	git update-index --add twelve &&
	test $(ls .git/sharedindex.* | wc -l) -le 2
'

test_expect_success 'check splitIndex.sharedIndexExpire set to 16 days' '
	git config splitIndex.sharedIndexExpire "16.days.ago" &&
	test-tool chmtime =$just_over_2_weeks_ago .git/sharedindex.* &&
	create_non_racy_file thirteen &&
	git update-index --add thirteen &&
	test $(ls .git/sharedindex.* | wc -l) -gt 2 &&
	just_over_16_days_ago=$((-1-16*86400)) &&
	test-tool chmtime =$just_over_16_days_ago .git/sharedindex.* &&
	create_non_racy_file fourteen &&
	git update-index --add fourteen &&
	test $(ls .git/sharedindex.* | wc -l) -le 2
'

test_expect_success 'check splitIndex.sharedIndexExpire set to "never" and "now"' '
	git config splitIndex.sharedIndexExpire never &&
	just_10_years_ago=$((-365*10*86400)) &&
	test-tool chmtime =$just_10_years_ago .git/sharedindex.* &&
	create_non_racy_file fifteen &&
	git update-index --add fifteen &&
	test $(ls .git/sharedindex.* | wc -l) -gt 2 &&
	git config splitIndex.sharedIndexExpire now &&
	just_1_second_ago=-1 &&
	test-tool chmtime =$just_1_second_ago .git/sharedindex.* &&
	create_non_racy_file sixteen &&
	git update-index --add sixteen &&
	test $(ls .git/sharedindex.* | wc -l) -le 2
'

test_expect_success POSIXPERM 'same mode for index & split index' '
	git init same-mode &&
	(
		cd same-mode &&
		test_commit A &&
		test_modebits .git/index >index_mode &&
		test_must_fail git config core.sharedRepository &&
		git -c core.splitIndex=true status &&
		shared=$(ls .git/sharedindex.*) &&
		case "$shared" in
		*" "*)
			# we have more than one???
			false ;;
		*)
			test_modebits "$shared" >split_index_mode &&
			test_cmp index_mode split_index_mode ;;
		esac
	)
'

while read -r mode modebits
do
	test_expect_success POSIXPERM "split index respects core.sharedrepository $mode" '
		# Remove existing shared index files
		git config core.splitIndex false &&
		git update-index --force-remove one &&
		rm -f .git/sharedindex.* &&
		# Create one new shared index file
		git config core.sharedrepository "$mode" &&
		git config core.splitIndex true &&
		create_non_racy_file one &&
		git update-index --add one &&
		echo "$modebits" >expect &&
		test_modebits .git/index >actual &&
		test_cmp expect actual &&
		shared=$(ls .git/sharedindex.*) &&
		case "$shared" in
		*" "*)
			# we have more than one???
			false ;;
		*)
			test_modebits "$shared" >actual &&
			test_cmp expect actual ;;
		esac
	'
done <<\EOF
0666 -rw-rw-rw-
0642 -rw-r---w-
EOF

test_expect_success POSIXPERM,SANITY 'graceful handling when splitting index is not allowed' '
	test_create_repo ro &&
	(
		cd ro &&
		test_commit initial &&
		git update-index --split-index &&
		test -f .git/sharedindex.*
	) &&
	cp ro/.git/index new-index &&
	test_when_finished "chmod u+w ro/.git" &&
	chmod u-w ro/.git &&
	GIT_INDEX_FILE="$(pwd)/new-index" git -C ro update-index --split-index &&
	chmod u+w ro/.git &&
	rm ro/.git/sharedindex.* &&
	GIT_INDEX_FILE=new-index git ls-files >actual &&
	echo initial.t >expected &&
	test_cmp expected actual
'

test_expect_success 'writing split index with null sha1 does not write cache tree' '
	git config core.splitIndex true &&
	git config splitIndex.maxPercentChange 0 &&
	git commit -m "commit" &&
	{
		git ls-tree HEAD &&
		printf "160000 commit $ZERO_OID\\tbroken\\n"
	} >broken-tree &&
	echo "add broken entry" >msg &&

	tree=$(git mktree <broken-tree) &&
	test_tick &&
	commit=$(git commit-tree $tree -p HEAD <msg) &&
	git update-ref HEAD "$commit" &&
	GIT_ALLOW_NULL_SHA1=1 git reset --hard &&
	test_might_fail test-tool dump-cache-tree >cache-tree.out &&
	test_line_count = 0 cache-tree.out
'

test_expect_success 'do not refresh null base index' '
	test_create_repo merge &&
	(
		cd merge &&
		test_commit initial &&
		git checkout -b side-branch &&
		test_commit extra &&
		git checkout main &&
		git update-index --split-index &&
		test_commit more &&
		# must not write a new shareindex, or we wont catch the problem
		git -c splitIndex.maxPercentChange=100 merge --no-edit side-branch 2>err &&
		# i.e. do not expect warnings like
		# could not freshen shared index .../shareindex.00000...
		test_must_be_empty err
	)
'

test_expect_success 'reading split index at alternate location' '
	git init reading-alternate-location &&
	(
		cd reading-alternate-location &&
		>file-in-alternate &&
		git update-index --split-index --add file-in-alternate
	) &&
	echo file-in-alternate >expect &&

	# Should be able to find the shared index both right next to
	# the specified split index file ...
	GIT_INDEX_FILE=./reading-alternate-location/.git/index \
	git ls-files --cached >actual &&
	test_cmp expect actual &&

	# ... and, for backwards compatibility, in the current GIT_DIR
	# as well.
	mv -v ./reading-alternate-location/.git/sharedindex.* .git &&
	GIT_INDEX_FILE=./reading-alternate-location/.git/index \
	git ls-files --cached >actual &&
	test_cmp expect actual
'

test_expect_success 'GIT_TEST_SPLIT_INDEX works' '
	git init git-test-split-index &&
	(
		cd git-test-split-index &&
		>file &&
		GIT_TEST_SPLIT_INDEX=1 git update-index --add file &&
		ls -l .git/sharedindex.* >actual &&
		test_line_count = 1 actual
	)
'

test_done
