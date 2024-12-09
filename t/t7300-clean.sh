#!/bin/sh
#
# Copyright (c) 2007 Michael Spang
#

test_description='git clean basic tests'

. ./test-lib.sh

git config clean.requireForce no

test_expect_success 'setup' '

	mkdir -p src &&
	touch src/part1.c Makefile &&
	echo build >.gitignore &&
	echo \*.o >>.gitignore &&
	git add . &&
	git commit -m setup &&
	touch src/part2.c README &&
	git add .

'

test_expect_success 'git clean with skip-worktree .gitignore' '
	git update-index --skip-worktree .gitignore &&
	rm .gitignore &&
	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so &&
	git update-index --no-skip-worktree .gitignore &&
	git checkout .gitignore
'

test_expect_success 'git clean' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean src/' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean src/ &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean src/ src/' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean src/ src/ &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean with prefix' '

	mkdir -p build docs src/test &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so src/test/1.c &&
	(cd src/ && git clean) &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_file src/test/1.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean with relative prefix' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	would_clean=$(
		cd docs &&
		git clean -n ../src |
		grep part3 |
		sed -n -e "s|^Would remove ||p"
	) &&
	test "$would_clean" = ../src/part3.c
'

test_expect_success 'git clean with absolute path' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	would_clean=$(
		cd docs &&
		git clean -n "$(pwd)/../src" |
		grep part3 |
		sed -n -e "s|^Would remove ||p"
	) &&
	test "$would_clean" = ../src/part3.c
'

test_expect_success 'git clean with out of work tree relative path' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	(
		cd docs &&
		test_must_fail git clean -n ../..
	)
'

test_expect_success 'git clean with out of work tree absolute path' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	dd=$(cd .. && pwd) &&
	(
		cd docs &&
		test_must_fail git clean -n $dd
	)
'

test_expect_success 'git clean -d with prefix and path' '

	mkdir -p build docs src/feature &&
	touch a.out src/part3.c src/feature/file.c docs/manual.txt obj.o build/lib.so &&
	(cd src/ && git clean -d feature/) &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_file src/part3.c &&
	test_path_is_missing src/feature/file.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success SYMLINKS 'git clean symbolic link' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	ln -s docs/manual.txt src/part4.c &&
	git clean &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_missing src/part4.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean with wildcard' '

	touch a.clean b.clean other.c &&
	git clean "*.clean" &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.clean &&
	test_path_is_missing b.clean &&
	test_path_is_file other.c

'

test_expect_success 'git clean -n' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -n &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_file src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean -d' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_missing docs &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean -d src/ examples/' '

	mkdir -p build docs examples &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so examples/1.c &&
	git clean -d src/ examples/ &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_missing examples/1.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean -x' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -x &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_missing obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean -d -x' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d -x &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_missing docs &&
	test_path_is_missing obj.o &&
	test_path_is_missing build

'

test_expect_success 'git clean -d -x with ignored tracked directory' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d -x -e src &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.out &&
	test_path_is_file src/part3.c &&
	test_path_is_missing docs &&
	test_path_is_missing obj.o &&
	test_path_is_missing build

'

test_expect_success 'git clean -X' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -X &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_file src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_missing obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'git clean -d -X' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d -X &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_file src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_missing obj.o &&
	test_path_is_missing build

'

test_expect_success 'git clean -d -X with ignored tracked directory' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d -X -e src &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_missing obj.o &&
	test_path_is_missing build

'

test_expect_success 'clean.requireForce defaults to true' '

	git config --unset clean.requireForce &&
	test_must_fail git clean

'

test_expect_success 'clean.requireForce' '

	git config clean.requireForce true &&
	test_must_fail git clean

'

test_expect_success 'clean.requireForce and -n' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -n &&
	test_path_is_file Makefile &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_file a.out &&
	test_path_is_file src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'clean.requireForce and -f' '

	git clean -f &&
	test_path_is_file README &&
	test_path_is_file src/part1.c &&
	test_path_is_file src/part2.c &&
	test_path_is_missing a.out &&
	test_path_is_missing src/part3.c &&
	test_path_is_file docs/manual.txt &&
	test_path_is_file obj.o &&
	test_path_is_file build/lib.so

'

test_expect_success 'clean.requireForce and --interactive' '
	git clean --interactive </dev/null >output 2>error &&
	test_grep ! "requireForce is true and" error &&
	test_grep "\*\*\* Commands \*\*\*" output
'

test_expect_success 'core.excludesfile' '

	echo excludes >excludes &&
	echo included >included &&
	git config core.excludesfile excludes &&
	output=$(git clean -n excludes included 2>&1) &&
	expr "$output" : ".*included" >/dev/null &&
	! expr "$output" : ".*excludes" >/dev/null

'

test_expect_success SANITY 'removal failure' '

	mkdir foo &&
	touch foo/bar &&
	test_when_finished "chmod 755 foo" &&
	(exec <foo/bar &&
	 chmod 0 foo &&
	 test_must_fail git clean -f -d)
'

test_expect_success 'nested git work tree' '
	rm -fr foo bar baz &&
	mkdir -p foo bar baz/boo &&
	(
		cd foo &&
		git init &&
		test_commit nested hello.world
	) &&
	(
		cd bar &&
		>goodbye.people
	) &&
	(
		cd baz/boo &&
		git init &&
		test_commit deeply.nested deeper.world
	) &&
	git clean -f -d &&
	test_path_is_file foo/.git/index &&
	test_path_is_file foo/hello.world &&
	test_path_is_file baz/boo/.git/index &&
	test_path_is_file baz/boo/deeper.world &&
	test_path_is_missing bar
'

test_expect_success 'should clean things that almost look like git but are not' '
	rm -fr almost_git almost_bare_git almost_submodule &&
	mkdir -p almost_git/.git/objects &&
	mkdir -p almost_git/.git/refs &&
	cat >almost_git/.git/HEAD <<-\EOF &&
	garbage
	EOF
	cp -r almost_git/.git/ almost_bare_git &&
	mkdir almost_submodule/ &&
	cat >almost_submodule/.git <<-\EOF &&
	garbage
	EOF
	test_when_finished "rm -rf almost_*" &&
	git clean -f -d &&
	test_path_is_missing almost_git &&
	test_path_is_missing almost_bare_git &&
	test_path_is_missing almost_submodule
'

test_expect_success 'should not clean submodules' '
	rm -fr repo to_clean sub1 sub2 &&
	mkdir repo to_clean &&
	(
		cd repo &&
		git init &&
		test_commit msg hello.world
	) &&
	test_config_global protocol.file.allow always &&
	git submodule add ./repo/.git sub1 &&
	git commit -m "sub1" &&
	git branch before_sub2 &&
	git submodule add ./repo/.git sub2 &&
	git commit -m "sub2" &&
	git checkout before_sub2 &&
	>to_clean/should_clean.this &&
	git clean -f -d &&
	test_path_is_file repo/.git/index &&
	test_path_is_file repo/hello.world &&
	test_path_is_file sub1/.git &&
	test_path_is_file sub1/hello.world &&
	test_path_is_file sub2/.git &&
	test_path_is_file sub2/hello.world &&
	test_path_is_missing to_clean
'

test_expect_success POSIXPERM,SANITY 'should avoid cleaning possible submodules' '
	rm -fr to_clean possible_sub1 &&
	mkdir to_clean possible_sub1 &&
	test_when_finished "rm -rf possible_sub*" &&
	echo "gitdir: foo" >possible_sub1/.git &&
	>possible_sub1/hello.world &&
	chmod 0 possible_sub1/.git &&
	>to_clean/should_clean.this &&
	git clean -f -d &&
	test_path_is_file possible_sub1/.git &&
	test_path_is_file possible_sub1/hello.world &&
	test_path_is_missing to_clean
'

test_expect_success 'nested (empty) git should be kept' '
	rm -fr empty_repo to_clean &&
	git init empty_repo &&
	mkdir to_clean &&
	>to_clean/should_clean.this &&
	# Note that we put the expect file in the .git directory so that it
	# does not get cleaned.
	find empty_repo | sort >.git/expect &&
	git clean -f -d &&
	find empty_repo | sort >actual &&
	test_cmp .git/expect actual &&
	test_path_is_missing to_clean
'

test_expect_success 'nested bare repositories should be cleaned' '
	rm -fr bare1 bare2 subdir &&
	git init --bare bare1 &&
	git clone --local --bare . bare2 &&
	mkdir subdir &&
	cp -r bare2 subdir/bare3 &&
	git clean -f -d &&
	test_path_is_missing bare1 &&
	test_path_is_missing bare2 &&
	test_path_is_missing subdir
'

test_expect_failure 'nested (empty) bare repositories should be cleaned even when in .git' '
	rm -fr strange_bare &&
	mkdir strange_bare &&
	git init --bare strange_bare/.git &&
	git clean -f -d &&
	test_path_is_missing strange_bare
'

test_expect_failure 'nested (non-empty) bare repositories should be cleaned even when in .git' '
	rm -fr strange_bare &&
	mkdir strange_bare &&
	git clone --local --bare . strange_bare/.git &&
	git clean -f -d &&
	test_path_is_missing strange_bare
'

test_expect_success 'giving path in nested git work tree will NOT remove it' '
	rm -fr repo &&
	mkdir repo &&
	(
		cd repo &&
		git init &&
		mkdir -p bar/baz &&
		test_commit msg bar/baz/hello.world
	) &&
	find repo | sort >expect &&
	git clean -f -d repo/bar/baz &&
	find repo | sort >actual &&
	test_cmp expect actual
'

test_expect_success 'giving path to nested .git will not remove it' '
	rm -fr repo &&
	mkdir repo untracked &&
	(
		cd repo &&
		git init &&
		test_commit msg hello.world
	) &&
	find repo | sort >expect &&
	git clean -f -d repo/.git &&
	find repo | sort >actual &&
	test_cmp expect actual &&
	test_path_is_dir untracked/
'

test_expect_success 'giving path to nested .git/ will NOT remove contents' '
	rm -fr repo untracked &&
	mkdir repo untracked &&
	(
		cd repo &&
		git init &&
		test_commit msg hello.world
	) &&
	find repo | sort >expect &&
	git clean -f -d repo/.git/ &&
	find repo | sort >actual &&
	test_cmp expect actual &&
	test_path_is_dir untracked/
'

test_expect_success 'force removal of nested git work tree' '
	rm -fr foo bar baz &&
	mkdir -p foo bar baz/boo &&
	(
		cd foo &&
		git init &&
		test_commit nested hello.world
	) &&
	(
		cd bar &&
		>goodbye.people
	) &&
	(
		cd baz/boo &&
		git init &&
		test_commit deeply.nested deeper.world
	) &&
	git clean -f -f -d &&
	test_path_is_missing foo &&
	test_path_is_missing bar &&
	test_path_is_missing baz
'

test_expect_success 'git clean -e' '
	rm -fr repo &&
	mkdir repo &&
	(
		cd repo &&
		git init &&
		touch known 1 2 3 &&
		git add known &&
		git clean -f -e 1 -e 2 &&
		test_path_exists 1 &&
		test_path_exists 2 &&
		test_path_is_missing 3 &&
		test_path_exists known
	)
'

test_expect_success SANITY 'git clean -d with an unreadable empty directory' '
	mkdir foo &&
	chmod a= foo &&
	git clean -dfx foo &&
	test_path_is_missing foo
'

test_expect_success 'git clean -d respects pathspecs (dir is prefix of pathspec)' '
	mkdir -p foo &&
	mkdir -p foobar &&
	git clean -df foobar &&
	test_path_is_dir foo &&
	test_path_is_missing foobar
'

test_expect_success 'git clean -d respects pathspecs (pathspec is prefix of dir)' '
	mkdir -p foo &&
	mkdir -p foobar &&
	git clean -df foo &&
	test_path_is_missing foo &&
	test_path_is_dir foobar
'

test_expect_success 'git clean -d skips untracked dirs containing ignored files' '
	echo /foo/bar >.gitignore &&
	echo ignoreme >>.gitignore &&
	rm -rf foo &&
	mkdir -p foo/a/aa/aaa foo/b/bb/bbb &&
	touch foo/bar foo/baz foo/a/aa/ignoreme foo/b/ignoreme foo/b/bb/1 foo/b/bb/2 &&
	git clean -df &&
	test_path_is_dir foo &&
	test_path_is_file foo/bar &&
	test_path_is_missing foo/baz &&
	test_path_is_file foo/a/aa/ignoreme &&
	test_path_is_missing foo/a/aa/aaa &&
	test_path_is_file foo/b/ignoreme &&
	test_path_is_missing foo/b/bb
'

test_expect_success 'git clean -d skips nested repo containing ignored files' '
	test_when_finished "rm -rf nested-repo-with-ignored-file" &&

	git init nested-repo-with-ignored-file &&
	(
		cd nested-repo-with-ignored-file &&
		>file &&
		git add file &&
		git commit -m Initial &&

		# This file is ignored by a .gitignore rule in the outer repo
		# added in the previous test.
		>ignoreme
	) &&

	git clean -fd &&

	test_path_is_file nested-repo-with-ignored-file/.git/index &&
	test_path_is_file nested-repo-with-ignored-file/ignoreme &&
	test_path_is_file nested-repo-with-ignored-file/file
'

test_expect_success 'git clean handles being told what to clean' '
	mkdir -p d1 d2 &&
	touch d1/ut d2/ut &&
	git clean -f */ut &&
	test_path_is_missing d1/ut &&
	test_path_is_missing d2/ut
'

test_expect_success 'git clean handles being told what to clean, with -d' '
	mkdir -p d1 d2 &&
	touch d1/ut d2/ut &&
	git clean -ffd */ut &&
	test_path_is_missing d1/ut &&
	test_path_is_missing d2/ut
'

test_expect_success 'git clean works if a glob is passed without -d' '
	mkdir -p d1 d2 &&
	touch d1/ut d2/ut &&
	git clean -f "*ut" &&
	test_path_is_missing d1/ut &&
	test_path_is_missing d2/ut
'

test_expect_success 'git clean works if a glob is passed with -d' '
	mkdir -p d1 d2 &&
	touch d1/ut d2/ut &&
	git clean -ffd "*ut" &&
	test_path_is_missing d1/ut &&
	test_path_is_missing d2/ut
'

test_expect_success MINGW 'handle clean & core.longpaths = false nicely' '
	test_config core.longpaths false &&
	a50=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa &&
	mkdir -p $a50$a50/$a50$a50/$a50$a50 &&
	: >"$a50$a50/test.txt" 2>"$a50$a50/$a50$a50/$a50$a50/test.txt" &&
	# create a temporary outside the working tree to hide from "git clean"
	test_must_fail git clean -xdf 2>.git/err &&
	# grepping for a strerror string is unportable but it is OK here with
	# MINGW prereq
	test_grep -e "too long" -e "No such file or directory" .git/err
'

test_expect_success 'clean untracked paths by pathspec' '
	git init untracked &&
	mkdir untracked/dir &&
	echo >untracked/dir/file.txt &&
	git -C untracked clean -f dir/file.txt &&
	ls untracked/dir >actual &&
	test_must_be_empty actual
'

test_expect_success 'avoid traversing into ignored directories' '
	test_when_finished rm -f output error trace.* &&
	test_create_repo avoid-traversing-deep-hierarchy &&
	(
		cd avoid-traversing-deep-hierarchy &&

		mkdir -p untracked/subdir/with/a &&
		>untracked/subdir/with/a/random-file.txt &&

		GIT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
		git clean -ffdxn -e untracked
	) &&

	# Make sure we only visited into the top-level directory, and did
	# not traverse into the "untracked" subdirectory since it was excluded
	grep data.*read_directo.*directories-visited trace.output |
		cut -d "|" -f 9 >trace.relevant &&
	cat >trace.expect <<-EOF &&
	 ..directories-visited:1
	EOF
	test_cmp trace.expect trace.relevant
'

test_expect_success 'traverse into directories that may have ignored entries' '
	test_when_finished rm -f output &&
	test_create_repo need-to-traverse-into-hierarchy &&
	(
		cd need-to-traverse-into-hierarchy &&
		mkdir -p modules/foobar/src/generated &&
		> modules/foobar/src/generated/code.c &&
		> modules/foobar/Makefile &&
		echo "/modules/**/src/generated/" >.gitignore &&

		git clean -fX modules/foobar >../output &&

		grep Removing ../output &&

		test_path_is_missing modules/foobar/src/generated/code.c &&
		test_path_is_file modules/foobar/Makefile
	)
'

test_done
