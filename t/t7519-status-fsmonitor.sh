#!/bin/sh

test_description='git status with file system watcher'

. ./test-lib.sh

clean_repo () {
	git reset --hard HEAD &&
	git clean -fd &&
	rm -f marker
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
	echo 6 >dir2/new &&
	git add new &&
	git add dir1/new &&
	git add dir2/new
}

# The test query-fsmonitor hook proc will output a marker file we can use to
# ensure the hook was actually used to generate the correct results.

# fsmonitor works correctly with or without the untracked cache
# but if it is available, we'll turn it on to ensure we test that
# codepath as well.

test_lazy_prereq UNTRACKED_CACHE '
	{ git update-index --test-untracked-cache; ret=$?; } &&
	test $ret -ne 1
'

if test_have_prereq UNTRACKED_CACHE; then
	git config core.untrackedcache true
else
	git config core.untrackedcache false
fi

test_expect_success 'setup' '
	mkdir -p .git/hooks &&
	: >tracked &&
	: >modified &&
	mkdir dir1 &&
	: >dir1/tracked &&
	: >dir1/modified &&
	mkdir dir2 &&
	: >dir2/tracked &&
	: >dir2/modified &&
	git add . &&
	test_tick &&
	git commit -m initial &&
	git config core.fsmonitor true &&
	cat >.gitignore <<-\EOF
	.gitignore
	expect*
	output*
	marker*
	EOF
'

# Ensure commands that call refresh_index() to move the index back in time
# properly invalidate the fsmonitor cache

test_expect_success 'refresh_index() invalidates fsmonitor cache' '
	git status &&
	test_path_is_missing marker &&
	dirty_repo &&
	write_script .git/hooks/query-fsmonitor<<-\EOF &&
	:>marker
	EOF
	git add . &&
	git commit -m "to reset" &&
	git status &&
	test_path_is_file marker &&
	git reset HEAD~1 &&
	rm -f marker &&
	git status >output &&
	test_path_is_file marker &&
	git -c core.fsmonitor=false status >expect &&
	test_i18ncmp expect output
'

# Now make sure it's actually skipping the check for modified and untracked
# files unless it is told about them.  Note, after "git reset --hard HEAD" no
# extensions exist other than 'TREE' so do a "git status" to get the extension
# written before testing the results.

test_expect_success "status doesn't detect unreported modifications" '
	write_script .git/hooks/query-fsmonitor<<-\EOF &&
	:>marker
	EOF
	clean_repo &&
	git status &&
	test_path_is_missing marker &&
	: >untracked &&
	echo 2 >dir1/modified &&
	git status >output &&
	test_path_is_file marker &&
	test_i18ngrep ! "Changes not staged for commit:" output &&
	test_i18ngrep ! "Untracked files:" output &&
	write_script .git/hooks/query-fsmonitor<<-\EOF &&
	:>marker
	printf "untracked\0"
	printf "dir1/modified\0"
	EOF
	rm -f marker &&
	git status >output &&
	test_path_is_file marker &&
	test_i18ngrep "Changes not staged for commit:" output &&
	test_i18ngrep "Untracked files:" output
'

# Status is well tested elsewhere so we'll just ensure that the results are
# the same when using core.fsmonitor. First call after turning on the option
# does a complete scan so we need to do two calls to ensure we test the new
# codepath.

test_expect_success 'status with core.untrackedcache false' '
	git config core.untrackedcache false &&
	write_script .git/hooks/query-fsmonitor<<-\EOF &&
	if [ $1 -ne 1 ]
	then
		echo -e "Unsupported query-fsmonitor hook version.\n" >&2
		exit 1;
	fi
	: >marker
	printf "untracked\0"
	printf "dir1/untracked\0"
	printf "dir2/untracked\0"
	printf "modified\0"
	printf "dir1/modified\0"
	printf "dir2/modified\0"
	printf "new\0""
	printf "dir1/new\0"
	printf "dir2/new\0"
	EOF
	clean_repo &&
	dirty_repo &&
	git -c core.fsmonitor=false status >expect &&
	clean_repo &&
	git status &&
	test_path_is_missing marker &&
	dirty_repo &&
	git status >output &&
	test_path_is_file marker &&
	test_i18ncmp expect output
'

if ! test_have_prereq UNTRACKED_CACHE; then
	skip_all='This system does not support untracked cache'
	test_done
fi

test_expect_success 'status with core.untrackedcache true' '
	git config core.untrackedcache true &&
	git -c core.fsmonitor=false status >expect &&
	clean_repo &&
	git status &&
	test_path_is_missing marker &&
	dirty_repo &&
	git status >output &&
	test_path_is_file marker &&
	test_i18ncmp expect output
'

test_done
