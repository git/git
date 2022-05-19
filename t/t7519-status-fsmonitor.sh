#!/bin/sh

test_description='but status with file system watcher'

. ./test-lib.sh

# Note, after "but reset --hard HEAD" no extensions exist other than 'TREE'
# "but update-index --fsmonitor" can be used to get the extension written
# before testing the results.

clean_repo () {
	but reset --hard HEAD &&
	but clean -fd
}

dirty_repo () {
	: >untracked &&
	: >dir1/untracked &&
	: >dir2/untracked &&
	echo 1 >modified &&
	echo 2 >dir1/modified &&
	echo 3 >dir2/modified &&
	echo 4 >new &&
	echo 5 >dir1/new &&
	echo 6 >dir2/new
}

write_integration_script () {
	test_hook --setup --clobber fsmonitor-test<<-\EOF
	if test "$#" -ne 2
	then
		echo "$0: exactly 2 arguments expected"
		exit 2
	fi
	if test "$1" != 2
	then
		echo "Unsupported core.fsmonitor hook version." >&2
		exit 1
	fi
	printf "last_update_token\0"
	printf "untracked\0"
	printf "dir1/untracked\0"
	printf "dir2/untracked\0"
	printf "modified\0"
	printf "dir1/modified\0"
	printf "dir2/modified\0"
	printf "new\0"
	printf "dir1/new\0"
	printf "dir2/new\0"
	EOF
}

test_lazy_prereq UNTRACKED_CACHE '
	{ but update-index --test-untracked-cache; ret=$?; } &&
	test $ret -ne 1
'

test_expect_success 'setup' '
	: >tracked &&
	: >modified &&
	mkdir dir1 &&
	: >dir1/tracked &&
	: >dir1/modified &&
	mkdir dir2 &&
	: >dir2/tracked &&
	: >dir2/modified &&
	but -c core.fsmonitor= add . &&
	but -c core.fsmonitor= cummit -m initial &&
	but config core.fsmonitor .but/hooks/fsmonitor-test &&
	cat >.butignore <<-\EOF
	.butignore
	expect*
	actual*
	marker*
	trace2*
	EOF
'

# test that the fsmonitor extension is off by default
test_expect_success 'fsmonitor extension is off by default' '
	test-tool dump-fsmonitor >actual &&
	grep "^no fsmonitor" actual
'

# test that "update-index --fsmonitor" adds the fsmonitor extension
test_expect_success 'update-index --fsmonitor" adds the fsmonitor extension' '
	but update-index --fsmonitor &&
	test-tool dump-fsmonitor >actual &&
	grep "^fsmonitor last update" actual
'

# test that "update-index --no-fsmonitor" removes the fsmonitor extension
test_expect_success 'update-index --no-fsmonitor" removes the fsmonitor extension' '
	but update-index --no-fsmonitor &&
	test-tool dump-fsmonitor >actual &&
	grep "^no fsmonitor" actual
'

cat >expect <<EOF &&
h dir1/modified
H dir1/tracked
h dir2/modified
H dir2/tracked
h modified
H tracked
EOF

# test that "update-index --fsmonitor-valid" sets the fsmonitor valid bit
test_expect_success 'update-index --fsmonitor-valid" sets the fsmonitor valid bit' '
	test_hook fsmonitor-test<<-\EOF &&
		printf "last_update_token\0"
	EOF
	but update-index --fsmonitor &&
	but update-index --fsmonitor-valid dir1/modified &&
	but update-index --fsmonitor-valid dir2/modified &&
	but update-index --fsmonitor-valid modified &&
	but ls-files -f >actual &&
	test_cmp expect actual
'

cat >expect <<EOF &&
H dir1/modified
H dir1/tracked
H dir2/modified
H dir2/tracked
H modified
H tracked
EOF

# test that "update-index --no-fsmonitor-valid" clears the fsmonitor valid bit
test_expect_success 'update-index --no-fsmonitor-valid" clears the fsmonitor valid bit' '
	but update-index --no-fsmonitor-valid dir1/modified &&
	but update-index --no-fsmonitor-valid dir2/modified &&
	but update-index --no-fsmonitor-valid modified &&
	but ls-files -f >actual &&
	test_cmp expect actual
'

cat >expect <<EOF &&
H dir1/modified
H dir1/tracked
H dir2/modified
H dir2/tracked
H modified
H tracked
EOF

# test that all files returned by the script get flagged as invalid
test_expect_success 'all files returned by integration script get flagged as invalid' '
	write_integration_script &&
	dirty_repo &&
	but update-index --fsmonitor &&
	but ls-files -f >actual &&
	test_cmp expect actual
'

cat >expect <<EOF &&
H dir1/modified
h dir1/new
H dir1/tracked
H dir2/modified
h dir2/new
H dir2/tracked
H modified
h new
H tracked
EOF

# test that newly added files are marked valid
test_expect_success 'newly added files are marked valid' '
	test_hook --setup --clobber fsmonitor-test<<-\EOF &&
		printf "last_update_token\0"
	EOF
	but add new &&
	but add dir1/new &&
	but add dir2/new &&
	but ls-files -f >actual &&
	test_cmp expect actual
'

cat >expect <<EOF &&
H dir1/modified
h dir1/new
h dir1/tracked
H dir2/modified
h dir2/new
h dir2/tracked
H modified
h new
h tracked
EOF

# test that all unmodified files get marked valid
test_expect_success 'all unmodified files get marked valid' '
	# modified files result in update-index returning 1
	test_must_fail but update-index --refresh --force-write-index &&
	but ls-files -f >actual &&
	test_cmp expect actual
'

cat >expect <<EOF &&
H dir1/modified
h dir1/tracked
h dir2/modified
h dir2/tracked
h modified
h tracked
EOF

# test that *only* files returned by the integration script get flagged as invalid
test_expect_success '*only* files returned by the integration script get flagged as invalid' '
	test_hook --clobber fsmonitor-test<<-\EOF &&
	printf "last_update_token\0"
	printf "dir1/modified\0"
	EOF
	clean_repo &&
	but update-index --refresh --force-write-index &&
	echo 1 >modified &&
	echo 2 >dir1/modified &&
	echo 3 >dir2/modified &&
	test_must_fail but update-index --refresh --force-write-index &&
	but ls-files -f >actual &&
	test_cmp expect actual
'

# Ensure commands that call refresh_index() to move the index back in time
# properly invalidate the fsmonitor cache
test_expect_success 'refresh_index() invalidates fsmonitor cache' '
	clean_repo &&
	dirty_repo &&
	write_integration_script &&
	but add . &&
	test_hook --clobber fsmonitor-test<<-\EOF &&
	EOF
	but cummit -m "to reset" &&
	but reset HEAD~1 &&
	but status >actual &&
	but -c core.fsmonitor= status >expect &&
	test_cmp expect actual
'

# test fsmonitor with and without preloadIndex
preload_values="false true"
for preload_val in $preload_values
do
	test_expect_success "setup preloadIndex to $preload_val" '
		but config core.preloadIndex $preload_val &&
		if test $preload_val = true
		then
			GIT_TEST_PRELOAD_INDEX=$preload_val && export GIT_TEST_PRELOAD_INDEX
		else
			sane_unset GIT_TEST_PRELOAD_INDEX
		fi
	'

	# test fsmonitor with and without the untracked cache (if available)
	uc_values="false"
	test_have_prereq UNTRACKED_CACHE && uc_values="false true"
	for uc_val in $uc_values
	do
		test_expect_success "setup untracked cache to $uc_val" '
			but config core.untrackedcache $uc_val
		'

		# Status is well tested elsewhere so we'll just ensure that the results are
		# the same when using core.fsmonitor.
		test_expect_success 'compare status with and without fsmonitor' '
			write_integration_script &&
			clean_repo &&
			dirty_repo &&
			but add new &&
			but add dir1/new &&
			but add dir2/new &&
			but status >actual &&
			but -c core.fsmonitor= status >expect &&
			test_cmp expect actual
		'

		# Make sure it's actually skipping the check for modified and untracked
		# (if enabled) files unless it is told about them.
		test_expect_success "status doesn't detect unreported modifications" '
			test_hook --clobber fsmonitor-test<<-\EOF &&
			printf "last_update_token\0"
			:>marker
			EOF
			clean_repo &&
			but status &&
			test_path_is_file marker &&
			dirty_repo &&
			rm -f marker &&
			but status >actual &&
			test_path_is_file marker &&
			test_i18ngrep ! "Changes not staged for cummit:" actual &&
			if test $uc_val = true
			then
				test_i18ngrep ! "Untracked files:" actual
			fi &&
			if test $uc_val = false
			then
				test_i18ngrep "Untracked files:" actual
			fi &&
			rm -f marker
		'
	done
done

# test that splitting the index doesn't interfere
test_expect_success 'splitting the index results in the same state' '
	write_integration_script &&
	dirty_repo &&
	but update-index --fsmonitor  &&
	but ls-files -f >expect &&
	test-tool dump-fsmonitor >&2 && echo &&
	but update-index --fsmonitor --split-index &&
	test-tool dump-fsmonitor >&2 && echo &&
	but ls-files -f >actual &&
	test_cmp expect actual
'

test_expect_success UNTRACKED_CACHE 'ignore .but changes when invalidating UNTR' '
	test_create_repo dot-but &&
	(
		cd dot-but &&
		: >tracked &&
		test-tool chmtime =-60 tracked &&
		: >modified &&
		test-tool chmtime =-60 modified &&
		mkdir dir1 &&
		: >dir1/tracked &&
		test-tool chmtime =-60 dir1/tracked &&
		: >dir1/modified &&
		test-tool chmtime =-60 dir1/modified &&
		mkdir dir2 &&
		: >dir2/tracked &&
		test-tool chmtime =-60 dir2/tracked &&
		: >dir2/modified &&
		test-tool chmtime =-60 dir2/modified &&
		write_integration_script &&
		but config core.fsmonitor .but/hooks/fsmonitor-test &&
		but update-index --untracked-cache &&
		but update-index --fsmonitor &&
		but status &&
		GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace-before" \
		but status &&
		test-tool dump-untracked-cache >../before
	) &&
	cat >>dot-but/.but/hooks/fsmonitor-test <<-\EOF &&
	printf ".but\0"
	printf ".but/index\0"
	printf "dir1/.but\0"
	printf "dir1/.but/index\0"
	EOF
	(
		cd dot-but &&
		GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace-after" \
		but status &&
		test-tool dump-untracked-cache >../after
	) &&
	grep "directory-invalidation" trace-before | cut -d"|" -f 9 >>before &&
	grep "directory-invalidation" trace-after  | cut -d"|" -f 9 >>after &&
	# UNTR extension unchanged, dir invalidation count unchanged
	test_cmp before after
'

test_expect_success 'discard_index() also discards fsmonitor info' '
	test_config core.fsmonitor "$TEST_DIRECTORY/t7519/fsmonitor-all" &&
	test_might_fail but update-index --refresh &&
	test-tool read-cache --print-and-refresh=tracked 2 >actual &&
	printf "tracked is%s up to date\n" "" " not" >expect &&
	test_cmp expect actual
'

# Test unstaging entries that:
#  - Are not flagged with CE_FSMONITOR_VALID
#  - Have a position in the index >= the number of entries present in the index
#    after unstaging.
test_expect_success 'status succeeds after staging/unstaging' '
	test_create_repo fsmonitor-stage-unstage &&
	(
		cd fsmonitor-stage-unstage &&
		test_cummit initial &&
		but update-index --fsmonitor &&
		removed=$(test_seq 1 100 | sed "s/^/z/") &&
		touch $removed &&
		but add $removed &&
		but config core.fsmonitor "$TEST_DIRECTORY/t7519/fsmonitor-env" &&
		FSMONITOR_LIST="$removed" but restore -S $removed &&
		FSMONITOR_LIST="$removed" but status
	)
'

# Usage:
# check_sparse_index_behavior [!]
# If "!" is supplied, then we verify that we do not call ensure_full_index
# during a call to 'but status'. Otherwise, we verify that we _do_ call it.
check_sparse_index_behavior () {
	but -C full status --porcelain=v2 >expect &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
		but -C sparse status --porcelain=v2 >actual &&
	test_region $1 index ensure_full_index trace2.txt &&
	test_region fsm_hook query trace2.txt &&
	test_cmp expect actual &&
	rm trace2.txt
}

test_expect_success 'status succeeds with sparse index' '
	(
		sane_unset GIT_TEST_SPLIT_INDEX &&

		but clone . full &&
		but clone --sparse . sparse &&
		but -C sparse sparse-checkout init --cone --sparse-index &&
		but -C sparse sparse-checkout set dir1 dir2 &&

		test_hook --clobber fsmonitor-test <<-\EOF &&
			printf "last_update_token\0"
		EOF
		but -C full config core.fsmonitor ../.but/hooks/fsmonitor-test &&
		but -C sparse config core.fsmonitor ../.but/hooks/fsmonitor-test &&
		check_sparse_index_behavior ! &&

		test_hook --clobber fsmonitor-test <<-\EOF &&
			printf "last_update_token\0"
			printf "dir1/modified\0"
		EOF
		check_sparse_index_behavior ! &&

		but -C sparse sparse-checkout add dir1a &&

		for repo in full sparse
		do
			cp -r $repo/dir1 $repo/dir1a &&
			but -C $repo add dir1a &&
			but -C $repo cummit -m "add dir1a" || return 1
		done &&
		but -C sparse sparse-checkout set dir1 dir2 &&

		# This one modifies outside the sparse-checkout definition
		# and hence we expect to expand the sparse-index.
		test_hook --clobber fsmonitor-test <<-\EOF &&
			printf "last_update_token\0"
			printf "dir1a/modified\0"
		EOF
		check_sparse_index_behavior
	)
'

test_done
