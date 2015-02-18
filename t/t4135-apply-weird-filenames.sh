#!/bin/sh

test_description='git apply with weird postimage filenames'

. ./test-lib.sh

test_expect_success 'setup' '
	vector=$TEST_DIRECTORY/t4135 &&

	test_tick &&
	git commit --allow-empty -m preimage &&
	git tag preimage &&

	reset_preimage() {
		git checkout -f preimage^0 &&
		git read-tree -u --reset HEAD &&
		git update-index --refresh
	} &&

	test_when_finished "rm -f \"tab	embedded.txt\"" &&
	test_when_finished "rm -f '\''\"quoteembedded\".txt'\''" &&
	if test_have_prereq !MINGW &&
		touch -- "tab	embedded.txt" '\''"quoteembedded".txt'\''
	then
		test_set_prereq FUNNYNAMES
	fi
'

try_filename() {
	desc=$1
	postimage=$2
	prereq=${3:-}
	exp1=${4:-success}
	exp2=${5:-success}
	exp3=${6:-success}

	test_expect_$exp1 $prereq "$desc, git-style file creation patch" "
		echo postimage >expected &&
		reset_preimage &&
		rm -f '$postimage' &&
		git apply -v \"\$vector\"/'git-$desc.diff' &&
		test_cmp expected '$postimage'
	"

	test_expect_$exp2 $prereq "$desc, traditional patch" "
		echo postimage >expected &&
		reset_preimage &&
		echo preimage >'$postimage' &&
		git apply -v \"\$vector\"/'diff-$desc.diff' &&
		test_cmp expected '$postimage'
	"

	test_expect_$exp3 $prereq "$desc, traditional file creation patch" "
		echo postimage >expected &&
		reset_preimage &&
		rm -f '$postimage' &&
		git apply -v \"\$vector\"/'add-$desc.diff' &&
		test_cmp expected '$postimage'
	"
}

try_filename 'plain'            'postimage.txt'
try_filename 'with spaces'      'post image.txt'
try_filename 'with tab'         'post	image.txt' FUNNYNAMES
try_filename 'with backslash'   'post\image.txt' BSLASHPSPEC
try_filename 'with quote'       '"postimage".txt' FUNNYNAMES success failure success

test_expect_success 'whitespace-damaged traditional patch' '
	echo postimage >expected &&
	reset_preimage &&
	rm -f postimage.txt &&
	git apply -v "$vector/damaged.diff" &&
	test_cmp expected postimage.txt
'

test_expect_success 'traditional patch with colon in timezone' '
	echo postimage >expected &&
	reset_preimage &&
	rm -f "post image.txt" &&
	git apply "$vector/funny-tz.diff" &&
	test_cmp expected "post image.txt"
'

test_expect_success 'traditional, whitespace-damaged, colon in timezone' '
	echo postimage >expected &&
	reset_preimage &&
	rm -f "post image.txt" &&
	git apply "$vector/damaged-tz.diff" &&
	test_cmp expected "post image.txt"
'

test_done
