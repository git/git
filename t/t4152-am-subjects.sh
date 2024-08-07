#!/bin/sh

test_description='test subject preservation with format-patch | am'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

make_patches() {
	type=$1
	subject=$2
	test_expect_success "create patches with $type subject" '
		git reset --hard baseline &&
		echo $type >file &&
		git commit -a -m "$subject" &&
		git format-patch -1 --stdout >$type.patch &&
		git format-patch -1 --stdout -k >$type-k.patch
	'
}

check_subject() {
	git reset --hard baseline &&
	git am $2 $1.patch &&
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
}

test_expect_success 'setup baseline commit' '
	test_commit baseline file
'

SHORT_SUBJECT='short subject'
make_patches short "$SHORT_SUBJECT"

LONG_SUBJECT1='this is a long subject that is virtually guaranteed'
LONG_SUBJECT2='to require wrapping via format-patch if it is all'
LONG_SUBJECT3='going to appear on a single line'
LONG_SUBJECT="$LONG_SUBJECT1 $LONG_SUBJECT2 $LONG_SUBJECT3"
make_patches long "$LONG_SUBJECT"

MULTILINE_SUBJECT="$LONG_SUBJECT1
$LONG_SUBJECT2
$LONG_SUBJECT3"
make_patches multiline "$MULTILINE_SUBJECT"

echo "$SHORT_SUBJECT" >expect
test_expect_success 'short subject preserved (format-patch | am)' '
	check_subject short
'
test_expect_success 'short subject preserved (format-patch -k | am)' '
	check_subject short-k
'
test_expect_success 'short subject preserved (format-patch -k | am -k)' '
	check_subject short-k -k
'

echo "$LONG_SUBJECT" >expect
test_expect_success 'long subject preserved (format-patch | am)' '
	check_subject long
'
test_expect_success 'long subject preserved (format-patch -k | am)' '
	check_subject long-k
'
test_expect_success 'long subject preserved (format-patch -k | am -k)' '
	check_subject long-k -k
'

echo "$LONG_SUBJECT" >expect
test_expect_success 'multiline subject unwrapped (format-patch | am)' '
	check_subject multiline
'
test_expect_success 'multiline subject unwrapped (format-patch -k | am)' '
	check_subject multiline-k
'
echo "$MULTILINE_SUBJECT" >expect
test_expect_success 'multiline subject preserved (format-patch -k | am -k)' '
	check_subject multiline-k -k
'

test_done
