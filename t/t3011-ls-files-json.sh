#!/bin/sh

test_description='ls-files dumping json'

. ./test-lib.sh

strip_number() {
	for name; do
		echo 's/\("'$name'":\) [0-9]\+/\1 <number>/' >>filter.sed
	done
}

strip_string() {
	for name; do
		echo 's/\("'$name'":\) ".*"/\1 <string>/' >>filter.sed
	done
}

compare_json() {
	git ls-files --debug-json >json &&
	sed -f filter.sed json >filtered &&
	test_cmp "$TEST_DIRECTORY"/t3011/"$1" filtered
}

test_expect_success 'setup' '
	mkdir sub &&
	echo one >one &&
	git add one &&
	echo 2 >sub/two &&
	git add sub/two &&

	git commit -m first &&
	git update-index --untracked-cache &&

	echo intent-to-add >ita &&
	git add -N ita &&

	strip_number ctime_sec ctime_nsec mtime_sec mtime_nsec &&
	strip_number device inode uid gid file_offset ext_size last_update &&
	strip_string oid ident
'

test_expect_success 'ls-files --json, main entries, UNTR and TREE' '
	compare_json basic
'

test_expect_success 'ls-files --json, split index' '
	git init split &&
	(
		cd split &&
		echo one >one &&
		git add one &&
		git update-index --split-index &&
		echo updated >>one &&
		test_must_fail git -c splitIndex.maxPercentChange=100 update-index --refresh &&
		cp ../filter.sed . &&
		compare_json split-index
	)
'

test_expect_success 'ls-files --json, fsmonitor extension ' '
	git init fsmonitor &&
	(
		cd fsmonitor &&
		echo one >one &&
		git add one &&
		git update-index --fsmonitor &&
		cp ../filter.sed . &&
		compare_json fsmonitor
	)
'

test_expect_success 'ls-files --json, rerere extension' '
	git init rerere &&
	(
		cd rerere &&
		mkdir fi &&
		test_commit initial fi/le first &&
		git branch side &&
		test_commit second fi/le second &&
		git checkout side &&
		test_commit third fi/le third &&
		git checkout master &&
		git config rerere.enabled true &&
		test_must_fail git merge side &&
		echo resolved >fi/le &&
		git add fi/le &&
		cp ../filter.sed . &&
		compare_json rerere
	)
'

test_expect_success !SINGLE_CPU 'ls-files --json and multicore extensions' '
	git init eoie &&
	(
		cd eoie &&
		git config index.threads 2 &&
		touch one two three four &&
		git add . &&
		cp ../filter.sed . &&
		strip_number offset &&
		compare_json eoie
	)
'

test_done
