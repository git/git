#!/bin/sh

test_description='test conversion filters on large files'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

set_attr() {
	test_when_finished 'rm -f .gitattributes' &&
	echo "* $*" >.gitattributes
}

check_input() {
	git read-tree --empty &&
	git add small large &&
	git cat-file blob :small >small.index &&
	git cat-file blob :large | head -n 1 >large.index &&
	test_cmp small.index large.index
}

check_output() {
	rm -f small large &&
	git checkout small large &&
	head -n 1 large >large.head &&
	test_cmp small large.head
}

test_expect_success 'setup input tests' '
	printf "\$Id: foo\$\\r\\n" >small &&
	cat small small >large &&
	git config core.bigfilethreshold 20 &&
	git config filter.test.clean "sed s/.*/CLEAN/"
'

test_expect_success 'autocrlf=true converts on input' '
	test_config core.autocrlf true &&
	check_input
'

test_expect_success 'eol=crlf converts on input' '
	set_attr eol=crlf &&
	check_input
'

test_expect_success 'ident converts on input' '
	set_attr ident &&
	check_input
'

test_expect_success 'user-defined filters convert on input' '
	set_attr filter=test &&
	check_input
'

test_expect_success 'setup output tests' '
	echo "\$Id\$" >small &&
	cat small small >large &&
	git add small large &&
	git config core.bigfilethreshold 7 &&
	git config filter.test.smudge "sed s/.*/SMUDGE/"
'

test_expect_success 'autocrlf=true converts on output' '
	test_config core.autocrlf true &&
	check_output
'

test_expect_success 'eol=crlf converts on output' '
	set_attr eol=crlf &&
	check_output
'

test_expect_success 'user-defined filters convert on output' '
	set_attr filter=test &&
	check_output
'

test_expect_success 'ident converts on output' '
	set_attr ident &&
	rm -f small large &&
	git checkout small large &&
	sed -n "s/Id: .*/Id: SHA/p" <small >small.clean &&
	head -n 1 large >large.head &&
	sed -n "s/Id: .*/Id: SHA/p" <large.head >large.clean &&
	test_cmp small.clean large.clean
'

# This smudge filter prepends 5GB of zeros to the file it checks out. This
# ensures that smudging doesn't mangle large files on 64-bit Windows.
test_expect_success EXPENSIVE,SIZE_T_IS_64BIT,!LONG_IS_64BIT \
		'files over 4GB convert on output' '
	test_commit test small "a small file" &&
	small_size=$(test_file_size small) &&
	test_config filter.makelarge.smudge \
		"test-tool genzeros $((5*1024*1024*1024)) && cat" &&
	echo "small filter=makelarge" >.gitattributes &&
	rm small &&
	git checkout -- small &&
	size=$(test_file_size small) &&
	test "$size" -eq $((5 * 1024 * 1024 * 1024 + $small_size))
'

# This clean filter writes down the size of input it receives. By checking against
# the actual size, we ensure that cleaning doesn't mangle large files on 64-bit Windows.
test_expect_success EXPENSIVE,SIZE_T_IS_64BIT,!LONG_IS_64BIT \
		'files over 4GB convert on input' '
	test-tool genzeros $((5*1024*1024*1024)) >big &&
	test_config filter.checklarge.clean "wc -c >big.size" &&
	echo "big filter=checklarge" >.gitattributes &&
	git add big &&
	test $(test_file_size big) -eq $(cat big.size)
'

test_done
