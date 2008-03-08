#!/bin/sh
#
# Copyright (c) 2007 Michael Spang
#

test_description='git-clean basic tests'

. ./test-lib.sh

git config clean.requireForce no

test_expect_success 'setup' '

	mkdir -p src &&
	touch src/part1.c Makefile &&
	echo build >.gitignore &&
	echo \*.o >>.gitignore &&
	git add . &&
	git-commit -m setup &&
	touch src/part2.c README &&
	git add .

'

test_expect_success 'git-clean' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean &&
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

test_expect_success 'git-clean src/' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean src/ &&
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

test_expect_success 'git-clean src/ src/' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean src/ src/ &&
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

test_expect_success 'git-clean with prefix' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	(cd src/ && git-clean) &&
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
test_expect_success 'git-clean -d with prefix and path' '

	mkdir -p build docs src/feature &&
	touch a.out src/part3.c src/feature/file.c docs/manual.txt obj.o build/lib.so &&
	(cd src/ && git-clean -d feature/) &&
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

test_expect_success 'git-clean symbolic link' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	ln -s docs/manual.txt src/part4.c
	git-clean &&
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

test_expect_success 'git-clean with wildcard' '

	touch a.clean b.clean other.c &&
	git-clean "*.clean" &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.clean &&
	test ! -f b.clean &&
	test -f other.c

'

test_expect_success 'git-clean -n' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean -n &&
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

test_expect_success 'git-clean -d' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean -d &&
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

test_expect_success 'git-clean -d src/ examples/' '

	mkdir -p build docs examples &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so examples/1.c &&
	git-clean -d src/ examples/ &&
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

test_expect_success 'git-clean -x' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean -x &&
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

test_expect_success 'git-clean -d -x' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean -d -x &&
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

test_expect_success 'git-clean -X' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean -X &&
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

test_expect_success 'git-clean -d -X' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean -d -X &&
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

test_expect_success 'clean.requireForce defaults to true' '

	git config --unset clean.requireForce &&
	! git-clean

'

test_expect_success 'clean.requireForce' '

	git config clean.requireForce true &&
	! git-clean

'

test_expect_success 'clean.requireForce and -n' '

	mkdir -p build docs &&
	touch a.out src/part3.c docs/manual.txt obj.o build/lib.so &&
	git-clean -n &&
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

	git-clean -f &&
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
	git config core.excludesfile excludes &&
	output=$(git clean -n excludes included 2>&1) &&
	expr "$output" : ".*included" >/dev/null &&
	! expr "$output" : ".*excludes" >/dev/null

'

test_expect_success 'removal failure' '

	mkdir foo &&
	touch foo/bar &&
	exec <foo/bar &&
	chmod 0 foo &&
	test_must_fail git clean -f -d

'
chmod 755 foo

test_done
