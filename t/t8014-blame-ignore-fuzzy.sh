#!/bin/sh

test_description='git blame ignore fuzzy heuristic'
. ./test-lib.sh

pick_author='s/^[0-9a-f^]* *(\([^ ]*\) .*/\1/'

# Each test is composed of 4 variables:
# titleN - the test name
# aN - the initial content
# bN - the final content
# expectedN - the line numbers from aN that we expect git blame
#             on bN to identify, or "Final" if bN itself should
#             be identified as the origin of that line.

# We start at test 2 because setup will show as test 1
title2="Regression test for partially overlapping search ranges"
cat <<EOF >a2
1
2
3
abcdef
5
6
7
ijkl
9
10
11
pqrs
13
14
15
wxyz
17
18
19
EOF
cat <<EOF >b2
abcde
ijk
pqr
wxy
EOF
cat <<EOF >expected2
4
8
12
16
EOF

title3="Combine 3 lines into 2"
cat <<EOF >a3
if ((maxgrow==0) ||
	( single_line_field && (field->dcols < maxgrow)) ||
	(!single_line_field && (field->drows < maxgrow)))
EOF
cat <<EOF >b3
if ((maxgrow == 0) || (single_line_field && (field->dcols < maxgrow)) ||
	(!single_line_field && (field->drows < maxgrow))) {
EOF
cat <<EOF >expected3
2
3
EOF

title4="Add curly brackets"
cat <<EOF >a4
	if (rows) *rows = field->rows;
	if (cols) *cols = field->cols;
	if (frow) *frow = field->frow;
	if (fcol) *fcol = field->fcol;
EOF
cat <<EOF >b4
	if (rows) {
		*rows = field->rows;
	}
	if (cols) {
		*cols = field->cols;
	}
	if (frow) {
		*frow = field->frow;
	}
	if (fcol) {
		*fcol = field->fcol;
	}
EOF
cat <<EOF >expected4
1
1
Final
2
2
Final
3
3
Final
4
4
Final
EOF


title5="Combine many lines and change case"
cat <<EOF >a5
for(row=0,pBuffer=field->buf;
	row<height;
	row++,pBuffer+=width )
{
	if ((len = (int)( After_End_Of_Data( pBuffer, width ) - pBuffer )) > 0)
	{
		wmove( win, row, 0 );
		waddnstr( win, pBuffer, len );
EOF
cat <<EOF >b5
for (Row = 0, PBuffer = field->buf; Row < Height; Row++, PBuffer += Width) {
	if ((Len = (int)(afterEndOfData(PBuffer, Width) - PBuffer)) > 0) {
		wmove(win, Row, 0);
		waddnstr(win, PBuffer, Len);
EOF
cat <<EOF >expected5
1
5
7
8
EOF

title6="Rename and combine lines"
cat <<EOF >a6
bool need_visual_update = ((form != (FORM *)0)      &&
	(form->status & _POSTED) &&
	(form->current==field));

if (need_visual_update)
	Synchronize_Buffer(form);

if (single_line_field)
{
	growth = field->cols * amount;
	if (field->maxgrow)
		growth = Minimum(field->maxgrow - field->dcols,growth);
	field->dcols += growth;
	if (field->dcols == field->maxgrow)
EOF
cat <<EOF >b6
bool NeedVisualUpdate = ((Form != (FORM *)0) && (Form->status & _POSTED) &&
	(Form->current == field));

if (NeedVisualUpdate) {
	synchronizeBuffer(Form);
}

if (SingleLineField) {
	Growth = field->cols * amount;
	if (field->maxgrow) {
		Growth = Minimum(field->maxgrow - field->dcols, Growth);
	}
	field->dcols += Growth;
	if (field->dcols == field->maxgrow) {
EOF
cat <<EOF >expected6
1
3
4
5
6
Final
7
8
10
11
12
Final
13
14
EOF

# Both lines match identically so position must be used to tie-break.
title7="Same line twice"
cat <<EOF >a7
abc
abc
EOF
cat <<EOF >b7
abcd
abcd
EOF
cat <<EOF >expected7
1
2
EOF

title8="Enforce line order"
cat <<EOF >a8
abcdef
ghijkl
ab
EOF
cat <<EOF >b8
ghijk
abcd
EOF
cat <<EOF >expected8
2
3
EOF

title9="Expand lines and rename variables"
cat <<EOF >a9
int myFunction(int ArgumentOne, Thing *ArgTwo, Blah XuglyBug) {
	Squiggle FabulousResult = squargle(ArgumentOne, *ArgTwo,
		XuglyBug) + EwwwGlobalWithAReallyLongNameYepTooLong;
	return FabulousResult * 42;
}
EOF
cat <<EOF >b9
int myFunction(int argument_one, Thing *arg_asdfgh,
	Blah xugly_bug) {
	Squiggle fabulous_result = squargle(argument_one,
		*arg_asdfgh, xugly_bug)
		+ g_ewww_global_with_a_really_long_name_yep_too_long;
	return fabulous_result * 42;
}
EOF
cat <<EOF >expected9
1
1
2
3
3
4
5
EOF

title10="Two close matches versus one less close match"
cat <<EOF >a10
abcdef
abcdef
ghijkl
EOF
cat <<EOF >b10
gh
abcdefx
EOF
cat <<EOF >expected10
Final
2
EOF

# The first line of b matches best with the last line of a, but the overall
# match is better if we match it with the first line of a.
title11="Piggy in the middle"
cat <<EOF >a11
abcdefg
ijklmn
abcdefgh
EOF
cat <<EOF >b11
abcdefghx
ijklm
EOF
cat <<EOF >expected11
1
2
EOF

title12="No trailing newline"
printf "abc\ndef" >a12
printf "abx\nstu" >b12
cat <<EOF >expected12
1
Final
EOF

title13="Reorder includes"
cat <<EOF >a13
#include "c.h"
#include "b.h"
#include "a.h"
#include "e.h"
#include "d.h"
EOF
cat <<EOF >b13
#include "a.h"
#include "b.h"
#include "c.h"
#include "d.h"
#include "e.h"
EOF
cat <<EOF >expected13
3
2
1
5
4
EOF

last_test=13

test_expect_success setup '
	for i in $(test_seq 2 $last_test)
	do
		# Append each line in a separate commit to make it easy to
		# check which original line the blame output relates to.

		line_count=0 &&
		while IFS= read line
		do
			line_count=$((line_count+1)) &&
			echo "$line" >>"$i" &&
			git add "$i" &&
			test_tick &&
			GIT_AUTHOR_NAME="$line_count" git commit -m "$line_count" || return 1
		done <"a$i"
	done &&

	for i in $(test_seq 2 $last_test)
	do
		# Overwrite the files with the final content.
		cp b$i $i &&
		git add $i || return 1
	done &&
	test_tick &&

	# Commit the final content all at once so it can all be
	# referred to with the same commit ID.
	GIT_AUTHOR_NAME=Final git commit -m Final &&

	IGNOREME=$(git rev-parse HEAD)
'

for i in $(test_seq 2 $last_test); do
	eval title="\$title$i"
	test_expect_success "$title" \
	"git blame -M9 --ignore-rev $IGNOREME $i >output &&
	sed -e \"$pick_author\" output >actual &&
	test_cmp expected$i actual"
done

# This invoked a null pointer dereference when the chunk callback was called
# with a zero length parent chunk and there were no more suspects.
test_expect_success 'Diff chunks with no suspects' '
	test_write_lines xy1 A B C xy1 >file &&
	git add file &&
	test_tick &&
	GIT_AUTHOR_NAME=1 git commit -m 1 &&

	test_write_lines xy2 A B xy2 C xy2 >file &&
	git add file &&
	test_tick &&
	GIT_AUTHOR_NAME=2 git commit -m 2 &&
	REV_2=$(git rev-parse HEAD) &&

	test_write_lines xy3 A >file &&
	git add file &&
	test_tick &&
	GIT_AUTHOR_NAME=3 git commit -m 3 &&
	REV_3=$(git rev-parse HEAD) &&

	test_write_lines 1 1 >expected &&

	git blame --ignore-rev $REV_2 --ignore-rev $REV_3 file >output &&
	sed -e "$pick_author" output >actual &&

	test_cmp expected actual
	'

test_expect_success 'position matching' '
	test_write_lines abc def >file2 &&
	git add file2 &&
	test_tick &&
	GIT_AUTHOR_NAME=1 git commit -m 1 &&

	test_write_lines abc def abc def >file2 &&
	git add file2 &&
	test_tick &&
	GIT_AUTHOR_NAME=2 git commit -m 2 &&

	test_write_lines abcx defx abcx defx >file2 &&
	git add file2 &&
	test_tick &&
	GIT_AUTHOR_NAME=3 git commit -m 3 &&
	REV_3=$(git rev-parse HEAD) &&

	test_write_lines abcy defy abcx defx >file2 &&
	git add file2 &&
	test_tick &&
	GIT_AUTHOR_NAME=4 git commit -m 4 &&
	REV_4=$(git rev-parse HEAD) &&

	test_write_lines 1 1 2 2 >expected &&

	git blame --ignore-rev $REV_3 --ignore-rev $REV_4 file2 >output &&
	sed -e "$pick_author" output >actual &&

	test_cmp expected actual
	'

# This fails if each blame entry is processed independently instead of
# processing each diff change in full.
test_expect_success 'preserve order' '
	test_write_lines bcde >file3 &&
	git add file3 &&
	test_tick &&
	GIT_AUTHOR_NAME=1 git commit -m 1 &&

	test_write_lines bcde fghij >file3 &&
	git add file3 &&
	test_tick &&
	GIT_AUTHOR_NAME=2 git commit -m 2 &&

	test_write_lines bcde fghij abcd >file3 &&
	git add file3 &&
	test_tick &&
	GIT_AUTHOR_NAME=3 git commit -m 3 &&

	test_write_lines abcdx fghijx bcdex >file3 &&
	git add file3 &&
	test_tick &&
	GIT_AUTHOR_NAME=4 git commit -m 4 &&
	REV_4=$(git rev-parse HEAD) &&

	test_write_lines abcdx fghijy bcdex >file3 &&
	git add file3 &&
	test_tick &&
	GIT_AUTHOR_NAME=5 git commit -m 5 &&
	REV_5=$(git rev-parse HEAD) &&

	test_write_lines 1 2 3 >expected &&

	git blame --ignore-rev $REV_4 --ignore-rev $REV_5 file3 >output &&
	sed -e "$pick_author" output >actual &&

	test_cmp expected actual
	'

test_done
