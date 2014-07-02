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
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so &&
	git update-index --no-skip-worktree .gitignore &&
	git checkout .gitignore
'

test_expect_success 'git clean' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean &&
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

test_expect_success 'git clean src/' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean src/ &&
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

test_expect_success 'git clean src/ src/' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean src/ src/ &&
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

test_expect_success 'git clean with prefix' '

	mkdir -p build docs src/test &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so src/test/1.c &&
	(cd src/ && git clean) &&
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

test_expect_success C_LOCALE_OUTPUT 'git clean with relative prefix' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	would_clean=$(
		cd docs &&
		git clean -n ../src |
		sed -n -e "s|^Would remove ||p"
	) &&
	test "$would_clean" = ../src/part3.c || {
		echo "OOps <$would_clean>"
		false
	}
'

test_expect_success C_LOCALE_OUTPUT 'git clean with absolute path' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	would_clean=$(
		cd docs &&
		git clean -n "$(pwd)/../src" |
		sed -n -e "s|^Would remove ||p"
	) &&
	test "$would_clean" = ../src/part3.c || {
		echo "OOps <$would_clean>"
		false
	}
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

test_expect_success SYMLINKS 'git clean symbolic link' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	ln -s docs/manual.txt src/part4.c &&
	git clean &&
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

test_expect_success 'git clean with wildcard' '

	touch a.clean b.clean other.c &&
	git clean "*.clean" &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.clean &&
	test ! -f b.clean &&
	test -f other.c

'

test_expect_success 'git clean -n' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -n &&
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

test_expect_success 'git clean -d' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d &&
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

test_expect_success 'git clean -d src/ examples/' '

	mkdir -p build docs examples &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so examples/1.c &&
	git clean -d src/ examples/ &&
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

test_expect_success 'git clean -x' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -x &&
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

test_expect_success 'git clean -d -x' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d -x &&
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

test_expect_success 'git clean -d -x with ignored tracked directory' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d -x -e src &&
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

test_expect_success 'git clean -X' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -X &&
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

test_expect_success 'git clean -d -X' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d -X &&
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

test_expect_success 'git clean -d -X with ignored tracked directory' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git clean -d -X -e src &&
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

	git clean -f &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success C_LOCALE_OUTPUT 'core.excludesfile' '

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
		>hello.world
		git add . &&
		git commit -a -m nested
	) &&
	(
		cd bar &&
		>goodbye.people
	) &&
	(
		cd baz/boo &&
		git init &&
		>deeper.world
		git add . &&
		git commit -a -m deeply.nested
	) &&
	git clean -f -d &&
	test -f foo/.git/index &&
	test -f foo/hello.world &&
	test -f baz/boo/.git/index &&
	test -f baz/boo/deeper.world &&
	! test -d bar
'

test_expect_success 'force removal of nested git work tree' '
	rm -fr foo bar baz &&
	mkdir -p foo bar baz/boo &&
	(
		cd foo &&
		git init &&
		>hello.world
		git add . &&
		git commit -a -m nested
	) &&
	(
		cd bar &&
		>goodbye.people
	) &&
	(
		cd baz/boo &&
		git init &&
		>deeper.world
		git add . &&
		git commit -a -m deeply.nested
	) &&
	git clean -f -f -d &&
	! test -d foo &&
	! test -d bar &&
	! test -d baz
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
		test -e 1 &&
		test -e 2 &&
		! (test -e 3) &&
		test -e known
	)
'

test_expect_success SANITY 'git clean -d with an unreadable empty directory' '
	mkdir foo &&
	chmod a= foo &&
	git clean -dfx foo &&
	! test -d foo
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

test_done
