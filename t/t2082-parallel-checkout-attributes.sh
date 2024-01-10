#!/bin/sh

test_description='parallel-checkout: attributes

Verify that parallel-checkout correctly creates files that require
conversions, as specified in .gitattributes. The main point here is
to check that the conv_attr data is correctly sent to the workers
and that it contains sufficient information to smudge files
properly (without access to the index or attribute stack).
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-parallel-checkout.sh"
. "$TEST_DIRECTORY/lib-encoding.sh"

test_expect_success 'parallel-checkout with ident' '
	set_checkout_config 2 0 &&
	git init ident &&
	(
		cd ident &&
		echo "A ident" >.gitattributes &&
		echo "\$Id\$" >A &&
		echo "\$Id\$" >B &&
		git add -A &&
		git commit -m id &&

		rm A B &&
		test_checkout_workers 2 git reset --hard &&
		hexsz=$(test_oid hexsz) &&
		grep -E "\\\$Id: [0-9a-f]{$hexsz} \\\$" A &&
		grep "\\\$Id\\\$" B
	)
'

test_expect_success 'parallel-checkout with re-encoding' '
	set_checkout_config 2 0 &&
	git init encoding &&
	(
		cd encoding &&
		echo text >utf8-text &&
		write_utf16 <utf8-text >utf16-text &&

		echo "A working-tree-encoding=UTF-16" >.gitattributes &&
		cp utf16-text A &&
		cp utf8-text B &&
		git add A B .gitattributes &&
		git commit -m encoding &&

		# Check that A is stored in UTF-8
		git cat-file -p :A >A.internal &&
		test_cmp_bin utf8-text A.internal &&

		rm A B &&
		test_checkout_workers 2 git checkout A B &&

		# Check that A (and only A) is re-encoded during checkout
		test_cmp_bin utf16-text A &&
		test_cmp_bin utf8-text B
	)
'

test_expect_success 'parallel-checkout with eol conversions' '
	set_checkout_config 2 0 &&
	git init eol &&
	(
		cd eol &&
		printf "multi\r\nline\r\ntext" >crlf-text &&
		printf "multi\nline\ntext" >lf-text &&

		git config core.autocrlf false &&
		echo "A eol=crlf" >.gitattributes &&
		cp crlf-text A &&
		cp lf-text B &&
		git add A B .gitattributes &&
		git commit -m eol &&

		# Check that A is stored with LF format
		git cat-file -p :A >A.internal &&
		test_cmp_bin lf-text A.internal &&

		rm A B &&
		test_checkout_workers 2 git checkout A B &&

		# Check that A (and only A) is converted to CRLF during checkout
		test_cmp_bin crlf-text A &&
		test_cmp_bin lf-text B
	)
'

# Entries that require an external filter are not eligible for parallel
# checkout. Check that both the parallel-eligible and non-eligible entries are
# properly writen in a single checkout operation.
#
test_expect_success 'parallel-checkout and external filter' '
	set_checkout_config 2 0 &&
	git init filter &&
	(
		cd filter &&
		write_script <<-\EOF rot13.sh &&
		tr \
		  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" \
		  "nopqrstuvwxyzabcdefghijklmNOPQRSTUVWXYZABCDEFGHIJKLM"
		EOF

		git config filter.rot13.clean "\"$(pwd)/rot13.sh\"" &&
		git config filter.rot13.smudge "\"$(pwd)/rot13.sh\"" &&
		git config filter.rot13.required true &&

		echo abcd >original &&
		echo nopq >rot13 &&

		echo "A filter=rot13" >.gitattributes &&
		cp original A &&
		cp original B &&
		cp original C &&
		git add A B C .gitattributes &&
		git commit -m filter &&

		# Check that A (and only A) was cleaned
		git cat-file -p :A >A.internal &&
		test_cmp rot13 A.internal &&
		git cat-file -p :B >B.internal &&
		test_cmp original B.internal &&
		git cat-file -p :C >C.internal &&
		test_cmp original C.internal &&

		rm A B C *.internal &&
		test_checkout_workers 2 git checkout A B C &&

		# Check that A (and only A) was smudged during checkout
		test_cmp original A &&
		test_cmp original B &&
		test_cmp original C
	)
'

# The delayed queue is independent from the parallel queue, and they should be
# able to work together in the same checkout process.
#
test_expect_success 'parallel-checkout and delayed checkout' '
	test_config_global filter.delay.process \
		"test-tool rot13-filter --always-delay --log=\"$(pwd)/delayed.log\" clean smudge delay" &&
	test_config_global filter.delay.required true &&

	echo "abcd" >original &&
	echo "nopq" >rot13 &&

	git init delayed &&
	(
		cd delayed &&
		echo "*.d filter=delay" >.gitattributes &&
		cp ../original W.d &&
		cp ../original X.d &&
		cp ../original Y &&
		cp ../original Z &&
		git add -A &&
		git commit -m delayed &&

		# Check that *.d files were cleaned
		git cat-file -p :W.d >W.d.internal &&
		test_cmp W.d.internal ../rot13 &&
		git cat-file -p :X.d >X.d.internal &&
		test_cmp X.d.internal ../rot13 &&
		git cat-file -p :Y >Y.internal &&
		test_cmp Y.internal ../original &&
		git cat-file -p :Z >Z.internal &&
		test_cmp Z.internal ../original &&

		rm *
	) &&

	set_checkout_config 2 0 &&
	test_checkout_workers 2 git -C delayed checkout -f &&
	verify_checkout delayed &&

	# Check that the *.d files got to the delay queue and were filtered
	grep "smudge W.d .* \[DELAYED\]" delayed.log &&
	grep "smudge X.d .* \[DELAYED\]" delayed.log &&
	test_cmp delayed/W.d original &&
	test_cmp delayed/X.d original &&

	# Check that the parallel-eligible entries went to the right queue and
	# were not filtered
	! grep "smudge Y .* \[DELAYED\]" delayed.log &&
	! grep "smudge Z .* \[DELAYED\]" delayed.log &&
	test_cmp delayed/Y original &&
	test_cmp delayed/Z original
'

test_done
