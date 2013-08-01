#!/bin/sh

test_description='git clean -i basic tests'

. ./test-lib.sh

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

test_expect_success 'git clean -i (c: clean hotkey)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	echo c | git clean -i &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test ! -f src/part3.h &&
	test ! -f src/part4.c &&
	test ! -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -i (cl: clean prefix)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	echo cl | git clean -i &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test ! -f src/part3.h &&
	test ! -f src/part4.c &&
	test ! -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -i (quit)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	echo quit | git clean -i &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f docs/manual.txt &&
	test -f src/part3.c &&
	test -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -i (Ctrl+D)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	echo "\04" | git clean -i &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f docs/manual.txt &&
	test -f src/part3.c &&
	test -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (filter all)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo f; echo "*"; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f docs/manual.txt &&
	test -f src/part3.c &&
	test -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (filter patterns)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo f; echo "part3.* *.out"; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test ! -f docs/manual.txt &&
	test -f src/part3.c &&
	test -f src/part3.h &&
	test ! -f src/part4.c &&
	test ! -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (filter patterns 2)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo f; echo "* !*.out"; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test -f docs/manual.txt &&
	test -f src/part3.c &&
	test -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (select - all)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo s; echo "*"; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test ! -f src/part3.h &&
	test ! -f src/part4.c &&
	test ! -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (select - none)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo s; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f docs/manual.txt &&
	test -f src/part3.c &&
	test -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (select - number)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo s; echo 3; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (select - number 2)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo s; echo 2 3; echo 5; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test ! -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test -f src/part3.h &&
	test ! -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (select - number 3)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo s; echo 3,4 5; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test ! -f src/part3.h &&
	test ! -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (select - filenames)' '

	mkdir -p build docs &&
	touch a.out foo.txt bar.txt baz.txt &&
	(echo s; echo a.out fo ba bar; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test ! -f a.out &&
	test ! -f foo.txt &&
	test ! -f bar.txt &&
	test -f baz.txt &&
	rm baz.txt

'

test_expect_success 'git clean -id (select - range)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo s; echo 1,3-4; echo 2; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f src/part3.c &&
	test ! -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test ! -f docs/manual.txt &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (select - range 2)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo s; echo 4- 1; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test -f docs/manual.txt &&
	test -f src/part3.c &&
	test ! -f src/part3.h &&
	test ! -f src/part4.c &&
	test ! -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (inverse select)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo s; echo "*"; echo -5- 1 -2; echo; echo c) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test ! -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (ask)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo a; echo Y; echo y; echo no; echo yes; echo bad; echo) | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f docs/manual.txt &&
	test -f src/part3.c &&
	test ! -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id (ask - Ctrl+D)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(echo a; echo Y; echo no; echo yes; echo "\04") | \
	git clean -id &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id with prefix and path (filter)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(cd build/ && \
	 (echo f; echo "docs"; echo "*.h"; echo ; echo c) | \
	 git clean -id ..) &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test -f src/part3.h &&
	test ! -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id with prefix and path (select by name)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(cd build/ && \
	 (echo s; echo "../docs/"; echo "../src/part3.c"; \
	  echo "../src/part4.c";  echo; echo c) | \
	 git clean -id ..) &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test -f a.out &&
	test ! -f docs/manual.txt &&
	test ! -f src/part3.c &&
	test -f src/part3.h &&
	test ! -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_expect_success 'git clean -id with prefix and path (ask)' '

	mkdir -p build docs &&
	touch a.out src/part3.c src/part3.h src/part4.c src/part4.h \
	docs/manual.txt obj.o build/lib.so &&
	(cd build/ && \
	 (echo a; echo Y; echo y; echo no; echo yes; echo bad; echo) | \
	 git clean -id ..) &&
	test -f Makefile &&
	test -f README &&
	test -f src/part1.c &&
	test -f src/part2.c &&
	test ! -f a.out &&
	test ! -f docs/manual.txt &&
	test -f src/part3.c &&
	test ! -f src/part3.h &&
	test -f src/part4.c &&
	test -f src/part4.h &&
	test -f obj.o &&
	test -f build/lib.so

'

test_done
