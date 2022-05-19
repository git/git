#!/bin/sh

test_description='applying patch that has broken whitespaces in context'

. ./test-lib.sh

test_expect_success setup '

	>file &&
	but add file &&

	# file-0 is full of whitespace breakages
	printf "%s \n" a bb c d eeee f ggg h >file-0 &&

	# patch-0 creates a whitespace broken file
	cat file-0 >file &&
	but diff >patch-0 &&
	but add file &&

	# file-1 is still full of whitespace breakages,
	# but has one line updated, without fixing any
	# whitespaces.
	# patch-1 records that change.
	sed -e "s/d/D/" file-0 >file-1 &&
	cat file-1 >file &&
	but diff >patch-1 &&

	# patch-all is the effect of both patch-0 and patch-1
	>file &&
	but add file &&
	cat file-1 >file &&
	but diff >patch-all &&

	# patch-2 is the same as patch-1 but is based
	# on a version that already has whitespace fixed,
	# and does not introduce whitespace breakages.
	sed -e "s/ \$//" patch-1 >patch-2 &&

	# If all whitespace breakages are fixed the contents
	# should look like file-fixed
	sed -e "s/ \$//" file-1 >file-fixed

'

test_expect_success nofix '

	>file &&
	but add file &&

	# Baseline.  Applying without fixing any whitespace
	# breakages.
	but apply --whitespace=nowarn patch-0 &&
	but apply --whitespace=nowarn patch-1 &&

	# The result should obviously match.
	test_cmp file-1 file
'

test_expect_success 'withfix (forward)' '

	>file &&
	but add file &&

	# The first application will munge the context lines
	# the second patch depends on.  We should be able to
	# adjust and still apply.
	but apply --whitespace=fix patch-0 &&
	but apply --whitespace=fix patch-1 &&

	test_cmp file-fixed file
'

test_expect_success 'withfix (backward)' '

	>file &&
	but add file &&

	# Now we have a whitespace breakages on our side.
	but apply --whitespace=nowarn patch-0 &&

	# And somebody sends in a patch based on image
	# with whitespace already fixed.
	but apply --whitespace=fix patch-2 &&

	# The result should accept the whitespace fixed
	# postimage.  But the line with "h" is beyond context
	# horizon and left unfixed.

	sed -e /h/d file-fixed >fixed-head &&
	sed -e /h/d file >file-head &&
	test_cmp fixed-head file-head &&

	sed -n -e /h/p file-fixed >fixed-tail &&
	sed -n -e /h/p file >file-tail &&

	! test_cmp fixed-tail file-tail

'

test_done
