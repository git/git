#!/bin/sh
#
# Copyright (c) 2007 Michael Spang
#

test_description='but clean basic tests'

. ./test-lib.sh

but config clean.requireForce no

test_expect_success 'setup' '

	mkdir -p src &&
	touch src/part1.c Makefile &&
	echo build >.butignore &&
	echo \*.o >>.butignore &&
	but add . &&
	but cummit -m setup &&
	touch src/part2.c README &&
	but add .

'

test_expect_success 'but clean with skip-worktree .butignore' '
	but update-index --skip-worktree .butignore &&
	rm .butignore &&
	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so &&
	but update-index --no-skip-worktree .butignore &&
	but checkout .butignore
'

test_expect_success 'but clean' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean src/' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean src/ &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean src/ src/' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean src/ src/ &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean with prefix' '

	mkdir -p build docs src/test &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so src/test/1.c &&
	(cd src/ && but clean) &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test ! -f src/part3.c &&
	test -f src/test/1.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean with relative prefix' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	would_clean=$(
		cd docs &&
		but clean -n ../src |
		grep part3 |
		sed -n -e "s|^Would remove ||p"
	) &&
	verbose test "$would_clean" = ../src/part3.c
'

test_expect_success 'but clean with absolute path' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	would_clean=$(
		cd docs &&
		but clean -n "$(pwd)/../src" |
		grep part3 |
		sed -n -e "s|^Would remove ||p"
	) &&
	verbose test "$would_clean" = ../src/part3.c
'

test_expect_success 'but clean with out of work tree relative path' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	(
		cd docs &&
		test_must_fail but clean -n ../..
	)
'

test_expect_success 'but clean with out of work tree absolute path' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	dd=$(cd .. && pwd) &&
	(
		cd docs &&
		test_must_fail but clean -n $dd
	)
'

test_expect_success 'but clean -d with prefix and path' '

	mkdir -p build docs src/feature &&
	touch a.out src/part3.c src/feature/file.c docs/manual.txt obj.o build/lib.so &&
	(cd src/ && but clean -d feature/) &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f src/part3.c &&
	test ! -f src/feature/file.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success SYMLINKS 'but clean symbolic link' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	ln -s docs/manual.txt src/part4.c &&
	but clean &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test ! -f src/part4.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean with wildcard' '

	touch a.clean b.clean other.c &&
	but clean "*.clean" &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.clean &&
	test ! -f b.clean &&
	test -f other.c

'

test_expect_success 'but clean -n' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -n &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean -d' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -d &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test ! -d docs &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean -d src/ examples/' '

	mkdir -p build docs examples &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so examples/1.c &&
	but clean -d src/ examples/ &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test ! -f src/part3.c &&
	test ! -f examples/1.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean -x' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -x &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test ! -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean -d -x' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -d -x &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test ! -d docs &&
	test ! -f obj.o &&
	test ! -d build

'

test_expect_success 'but clean -d -x with ignored tracked directory' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -d -x -e src &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test -f src/part3.c &&
	test ! -d docs &&
	test ! -f obj.o &&
	test ! -d build

'

test_expect_success 'but clean -X' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -X &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f src/part3.c &&
	test -f docs/manual.txt &&
	test ! -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'but clean -d -X' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -d -X &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f src/part3.c &&
	test -f docs/manual.txt &&
	test ! -f obj.o &&
	test ! -d build

'

test_expect_success 'but clean -d -X with ignored tracked directory' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -d -X -e src &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test ! -f obj.o &&
	test ! -d build

'

test_expect_success 'clean.requireForce defaults to true' '

	but config --unset clean.requireForce &&
	test_must_fail but clean

'

test_expect_success 'clean.requireForce' '

	but config clean.requireForce true &&
	test_must_fail but clean

'

test_expect_success 'clean.requireForce and -n' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	but clean -n &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'clean.requireForce and -f' '

	but clean -f &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'core.excludesfile' '

	echo excludes >excludes &&
	echo included >included &&
	but config core.excludesfile excludes &&
	output=$(but clean -n excludes included 2>&1) &&
	expr "$output" : ".*included" >/dev/null &&
	! expr "$output" : ".*excludes" >/dev/null

'

test_expect_success SANITY 'removal failure' '

	mkdir foo &&
	touch foo/bar &&
	test_when_finished "chmod 755 foo" &&
	(exec <foo/bar &&
	 chmod 0 foo &&
	 test_must_fail but clean -f -d)
'

test_expect_success 'nested but work tree' '
	rm -fr foo bar baz &&
	mkdir -p foo bar baz/boo &&
	(
		cd foo &&
		but init &&
		test_cummit nested hello.world
	) &&
	(
		cd bar &&
		>goodbye.people
	) &&
	(
		cd baz/boo &&
		but init &&
		test_cummit deeply.nested deeper.world
	) &&
	but clean -f -d &&
	test -f foo/.but/index &&
	test -f foo/hello.world &&
	test -f baz/boo/.but/index &&
	test -f baz/boo/deeper.world &&
	! test -d bar
'

test_expect_success 'should clean things that almost look like but but are not' '
	rm -fr almost_but almost_bare_but almost_submodule &&
	mkdir -p almost_but/.but/objects &&
	mkdir -p almost_but/.but/refs &&
	cat >almost_but/.but/HEAD <<-\EOF &&
	garbage
	EOF
	cp -r almost_but/.but/ almost_bare_but &&
	mkdir almost_submodule/ &&
	cat >almost_submodule/.but <<-\EOF &&
	garbage
	EOF
	test_when_finished "rm -rf almost_*" &&
	but clean -f -d &&
	test_path_is_missing almost_but &&
	test_path_is_missing almost_bare_but &&
	test_path_is_missing almost_submodule
'

test_expect_success 'should not clean submodules' '
	rm -fr repo to_clean sub1 sub2 &&
	mkdir repo to_clean &&
	(
		cd repo &&
		but init &&
		test_cummit msg hello.world
	) &&
	but submodule add ./repo/.but sub1 &&
	but cummit -m "sub1" &&
	but branch before_sub2 &&
	but submodule add ./repo/.but sub2 &&
	but cummit -m "sub2" &&
	but checkout before_sub2 &&
	>to_clean/should_clean.this &&
	but clean -f -d &&
	test_path_is_file repo/.but/index &&
	test_path_is_file repo/hello.world &&
	test_path_is_file sub1/.but &&
	test_path_is_file sub1/hello.world &&
	test_path_is_file sub2/.but &&
	test_path_is_file sub2/hello.world &&
	test_path_is_missing to_clean
'

test_expect_success POSIXPERM,SANITY 'should avoid cleaning possible submodules' '
	rm -fr to_clean possible_sub1 &&
	mkdir to_clean possible_sub1 &&
	test_when_finished "rm -rf possible_sub*" &&
	echo "butdir: foo" >possible_sub1/.but &&
	>possible_sub1/hello.world &&
	chmod 0 possible_sub1/.but &&
	>to_clean/should_clean.this &&
	but clean -f -d &&
	test_path_is_file possible_sub1/.but &&
	test_path_is_file possible_sub1/hello.world &&
	test_path_is_missing to_clean
'

test_expect_success 'nested (empty) but should be kept' '
	rm -fr empty_repo to_clean &&
	but init empty_repo &&
	mkdir to_clean &&
	>to_clean/should_clean.this &&
	but clean -f -d &&
	test_path_is_file empty_repo/.but/HEAD &&
	test_path_is_missing to_clean
'

test_expect_success 'nested bare repositories should be cleaned' '
	rm -fr bare1 bare2 subdir &&
	but init --bare bare1 &&
	but clone --local --bare . bare2 &&
	mkdir subdir &&
	cp -r bare2 subdir/bare3 &&
	but clean -f -d &&
	test_path_is_missing bare1 &&
	test_path_is_missing bare2 &&
	test_path_is_missing subdir
'

test_expect_failure 'nested (empty) bare repositories should be cleaned even when in .but' '
	rm -fr strange_bare &&
	mkdir strange_bare &&
	but init --bare strange_bare/.but &&
	but clean -f -d &&
	test_path_is_missing strange_bare
'

test_expect_failure 'nested (non-empty) bare repositories should be cleaned even when in .but' '
	rm -fr strange_bare &&
	mkdir strange_bare &&
	but clone --local --bare . strange_bare/.but &&
	but clean -f -d &&
	test_path_is_missing strange_bare
'

test_expect_success 'giving path in nested but work tree will NOT remove it' '
	rm -fr repo &&
	mkdir repo &&
	(
		cd repo &&
		but init &&
		mkdir -p bar/baz &&
		test_cummit msg bar/baz/hello.world
	) &&
	but clean -f -d repo/bar/baz &&
	test_path_is_file repo/.but/HEAD &&
	test_path_is_dir repo/bar/ &&
	test_path_is_file repo/bar/baz/hello.world
'

test_expect_success 'giving path to nested .but will not remove it' '
	rm -fr repo &&
	mkdir repo untracked &&
	(
		cd repo &&
		but init &&
		test_cummit msg hello.world
	) &&
	but clean -f -d repo/.but &&
	test_path_is_file repo/.but/HEAD &&
	test_path_is_dir repo/.but/refs &&
	test_path_is_dir repo/.but/objects &&
	test_path_is_dir untracked/
'

test_expect_success 'giving path to nested .but/ will NOT remove contents' '
	rm -fr repo untracked &&
	mkdir repo untracked &&
	(
		cd repo &&
		but init &&
		test_cummit msg hello.world
	) &&
	but clean -f -d repo/.but/ &&
	test_path_is_dir repo/.but &&
	test_path_is_file repo/.but/HEAD &&
	test_path_is_dir untracked/
'

test_expect_success 'force removal of nested but work tree' '
	rm -fr foo bar baz &&
	mkdir -p foo bar baz/boo &&
	(
		cd foo &&
		but init &&
		test_cummit nested hello.world
	) &&
	(
		cd bar &&
		>goodbye.people
	) &&
	(
		cd baz/boo &&
		but init &&
		test_cummit deeply.nested deeper.world
	) &&
	but clean -f -f -d &&
	! test -d foo &&
	! test -d bar &&
	! test -d baz
'

test_expect_success 'but clean -e' '
	rm -fr repo &&
	mkdir repo &&
	(
		cd repo &&
		but init &&
		touch known 1 2 3 &&
		but add known &&
		but clean -f -e 1 -e 2 &&
		test -e 1 &&
		test -e 2 &&
		! (test -e 3) &&
		test -e known
	)
'

test_expect_success SANITY 'but clean -d with an unreadable empty directory' '
	mkdir foo &&
	chmod a= foo &&
	but clean -dfx foo &&
	! test -d foo
'

test_expect_success 'but clean -d respects pathspecs (dir is prefix of pathspec)' '
	mkdir -p foo &&
	mkdir -p foobar &&
	but clean -df foobar &&
	test_path_is_dir foo &&
	test_path_is_missing foobar
'

test_expect_success 'but clean -d respects pathspecs (pathspec is prefix of dir)' '
	mkdir -p foo &&
	mkdir -p foobar &&
	but clean -df foo &&
	test_path_is_missing foo &&
	test_path_is_dir foobar
'

test_expect_success 'but clean -d skips untracked dirs containing ignored files' '
	echo /foo/bar >.butignore &&
	echo ignoreme >>.butignore &&
	rm -rf foo &&
	mkdir -p foo/a/aa/aaa foo/b/bb/bbb &&
	touch foo/bar foo/baz foo/a/aa/ignoreme foo/b/ignoreme foo/b/bb/1 foo/b/bb/2 &&
	but clean -df &&
	test_path_is_dir foo &&
	test_path_is_file foo/bar &&
	test_path_is_missing foo/baz &&
	test_path_is_file foo/a/aa/ignoreme &&
	test_path_is_missing foo/a/aa/aaa &&
	test_path_is_file foo/b/ignoreme &&
	test_path_is_missing foo/b/bb
'

test_expect_success 'but clean -d skips nested repo containing ignored files' '
	test_when_finished "rm -rf nested-repo-with-ignored-file" &&

	but init nested-repo-with-ignored-file &&
	(
		cd nested-repo-with-ignored-file &&
		>file &&
		but add file &&
		but cummit -m Initial &&

		# This file is ignored by a .butignore rule in the outer repo
		# added in the previous test.
		>ignoreme
	) &&

	but clean -fd &&

	test_path_is_file nested-repo-with-ignored-file/.but/index &&
	test_path_is_file nested-repo-with-ignored-file/ignoreme &&
	test_path_is_file nested-repo-with-ignored-file/file
'

test_expect_success 'but clean handles being told what to clean' '
	mkdir -p d1 d2 &&
	touch d1/ut d2/ut &&
	but clean -f */ut &&
	test_path_is_missing d1/ut &&
	test_path_is_missing d2/ut
'

test_expect_success 'but clean handles being told what to clean, with -d' '
	mkdir -p d1 d2 &&
	touch d1/ut d2/ut &&
	but clean -ffd */ut &&
	test_path_is_missing d1/ut &&
	test_path_is_missing d2/ut
'

test_expect_success 'but clean works if a glob is passed without -d' '
	mkdir -p d1 d2 &&
	touch d1/ut d2/ut &&
	but clean -f "*ut" &&
	test_path_is_missing d1/ut &&
	test_path_is_missing d2/ut
'

test_expect_success 'but clean works if a glob is passed with -d' '
	mkdir -p d1 d2 &&
	touch d1/ut d2/ut &&
	but clean -ffd "*ut" &&
	test_path_is_missing d1/ut &&
	test_path_is_missing d2/ut
'

test_expect_success MINGW 'handle clean & core.longpaths = false nicely' '
	test_config core.longpaths false &&
	a50=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa &&
	mkdir -p $a50$a50/$a50$a50/$a50$a50 &&
	: >"$a50$a50/test.txt" 2>"$a50$a50/$a50$a50/$a50$a50/test.txt" &&
	# create a temporary outside the working tree to hide from "but clean"
	test_must_fail but clean -xdf 2>.but/err &&
	# grepping for a strerror string is unportable but it is OK here with
	# MINGW prereq
	test_i18ngrep "too long" .but/err
'

test_expect_success 'clean untracked paths by pathspec' '
	but init untracked &&
	mkdir untracked/dir &&
	echo >untracked/dir/file.txt &&
	but -C untracked clean -f dir/file.txt &&
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

		BUT_TRACE2_PERF="$TRASH_DIRECTORY/trace.output" \
		but clean -ffdxn -e untracked
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
		echo "/modules/**/src/generated/" >.butignore &&

		but clean -fX modules/foobar >../output &&

		grep Removing ../output &&

		test_path_is_missing modules/foobar/src/generated/code.c &&
		test_path_is_file modules/foobar/Makefile
	)
'

test_done
