#!/bin/sh

test_description='git clean -i basic tests'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

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
	test_write_lines f "*" "" c |
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
	test_write_lines f "part3.* *.out" "" c |
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
	test_write_lines f "* !*.out" "" c |
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
	test_write_lines s "*" "" c |
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
	test_write_lines s "" c |
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
	test_write_lines s 3 "" c |
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
	test_write_lines s "2 3" 5 "" c |
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
	test_write_lines s "3,4 5" "" c |
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
	test_write_lines s "a.out fo ba bar" "" c |
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
	test_write_lines s "1,3-4" 2 "" c |
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
	test_write_lines s "4- 1" "" c |
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
	test_write_lines s "*" "-5- 1 -2" "" c |
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
	test_write_lines a Y y no yes bad "" |
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
	test_write_lines a Y no yes "\04" |
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
	(cd build/ &&
	 test_write_lines f docs "*.h" "" c |
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
	(cd build/ &&
	 test_write_lines s ../docs/ ../src/part3.c ../src/part4.c "" c |
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
	(cd build/ &&
	 test_write_lines a Y y no yes bad "" |
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

test_expect_success TTY 'git clean -i paints the header in HEADER color' '
	>a.out &&
	echo q |
	test_terminal git clean -i |
	test_decode_color |
	head -n 1 >header &&
	# not i18ngrep
	grep "^<BOLD>" header
'

test_done
