#!/bin/sh

test_description='diff --dirstat tests'
. ./test-lib.sh

# set up two commits where the second commit has these files
# (10 lines in each file):
#
#   unchanged/text           (unchanged from 1st commit)
#   changed/text             (changed 1st line)
#   rearranged/text          (swapped 1st and 2nd line)
#   dst/copy/unchanged/text  (copied from src/copy/unchanged/text, unchanged)
#   dst/copy/changed/text    (copied from src/copy/changed/text, changed)
#   dst/copy/rearranged/text (copied from src/copy/rearranged/text, rearranged)
#   dst/move/unchanged/text  (moved from src/move/unchanged/text, unchanged)
#   dst/move/changed/text    (moved from src/move/changed/text, changed)
#   dst/move/rearranged/text (moved from src/move/rearranged/text, rearranged)

test_expect_success 'setup' '
	mkdir unchanged &&
	mkdir changed &&
	mkdir rearranged &&
	mkdir src &&
	mkdir src/copy &&
	mkdir src/copy/unchanged &&
	mkdir src/copy/changed &&
	mkdir src/copy/rearranged &&
	mkdir src/move &&
	mkdir src/move/unchanged &&
	mkdir src/move/changed &&
	mkdir src/move/rearranged &&
	cat <<EOF >unchanged/text &&
unchanged       line #0
unchanged       line #1
unchanged       line #2
unchanged       line #3
unchanged       line #4
unchanged       line #5
unchanged       line #6
unchanged       line #7
unchanged       line #8
unchanged       line #9
EOF
	cat <<EOF >changed/text &&
changed         line #0
changed         line #1
changed         line #2
changed         line #3
changed         line #4
changed         line #5
changed         line #6
changed         line #7
changed         line #8
changed         line #9
EOF
	cat <<EOF >rearranged/text &&
rearranged      line #0
rearranged      line #1
rearranged      line #2
rearranged      line #3
rearranged      line #4
rearranged      line #5
rearranged      line #6
rearranged      line #7
rearranged      line #8
rearranged      line #9
EOF
	cat <<EOF >src/copy/unchanged/text &&
copy  unchanged line #0
copy  unchanged line #1
copy  unchanged line #2
copy  unchanged line #3
copy  unchanged line #4
copy  unchanged line #5
copy  unchanged line #6
copy  unchanged line #7
copy  unchanged line #8
copy  unchanged line #9
EOF
	cat <<EOF >src/copy/changed/text &&
copy    changed line #0
copy    changed line #1
copy    changed line #2
copy    changed line #3
copy    changed line #4
copy    changed line #5
copy    changed line #6
copy    changed line #7
copy    changed line #8
copy    changed line #9
EOF
	cat <<EOF >src/copy/rearranged/text &&
copy rearranged line #0
copy rearranged line #1
copy rearranged line #2
copy rearranged line #3
copy rearranged line #4
copy rearranged line #5
copy rearranged line #6
copy rearranged line #7
copy rearranged line #8
copy rearranged line #9
EOF
	cat <<EOF >src/move/unchanged/text &&
move  unchanged line #0
move  unchanged line #1
move  unchanged line #2
move  unchanged line #3
move  unchanged line #4
move  unchanged line #5
move  unchanged line #6
move  unchanged line #7
move  unchanged line #8
move  unchanged line #9
EOF
	cat <<EOF >src/move/changed/text &&
move    changed line #0
move    changed line #1
move    changed line #2
move    changed line #3
move    changed line #4
move    changed line #5
move    changed line #6
move    changed line #7
move    changed line #8
move    changed line #9
EOF
	cat <<EOF >src/move/rearranged/text &&
move rearranged line #0
move rearranged line #1
move rearranged line #2
move rearranged line #3
move rearranged line #4
move rearranged line #5
move rearranged line #6
move rearranged line #7
move rearranged line #8
move rearranged line #9
EOF
	git add . &&
	git commit -m "initial" &&
	mkdir dst &&
	mkdir dst/copy &&
	mkdir dst/copy/unchanged &&
	mkdir dst/copy/changed &&
	mkdir dst/copy/rearranged &&
	mkdir dst/move &&
	mkdir dst/move/unchanged &&
	mkdir dst/move/changed &&
	mkdir dst/move/rearranged &&
	cat <<EOF >changed/text &&
CHANGED XXXXXXX line #0
changed         line #1
changed         line #2
changed         line #3
changed         line #4
changed         line #5
changed         line #6
changed         line #7
changed         line #8
changed         line #9
EOF
	cat <<EOF >rearranged/text &&
rearranged      line #1
rearranged      line #0
rearranged      line #2
rearranged      line #3
rearranged      line #4
rearranged      line #5
rearranged      line #6
rearranged      line #7
rearranged      line #8
rearranged      line #9
EOF
	cat <<EOF >dst/copy/unchanged/text &&
copy  unchanged line #0
copy  unchanged line #1
copy  unchanged line #2
copy  unchanged line #3
copy  unchanged line #4
copy  unchanged line #5
copy  unchanged line #6
copy  unchanged line #7
copy  unchanged line #8
copy  unchanged line #9
EOF
	cat <<EOF >dst/copy/changed/text &&
copy XXXCHANGED line #0
copy    changed line #1
copy    changed line #2
copy    changed line #3
copy    changed line #4
copy    changed line #5
copy    changed line #6
copy    changed line #7
copy    changed line #8
copy    changed line #9
EOF
	cat <<EOF >dst/copy/rearranged/text &&
copy rearranged line #1
copy rearranged line #0
copy rearranged line #2
copy rearranged line #3
copy rearranged line #4
copy rearranged line #5
copy rearranged line #6
copy rearranged line #7
copy rearranged line #8
copy rearranged line #9
EOF
	cat <<EOF >dst/move/unchanged/text &&
move  unchanged line #0
move  unchanged line #1
move  unchanged line #2
move  unchanged line #3
move  unchanged line #4
move  unchanged line #5
move  unchanged line #6
move  unchanged line #7
move  unchanged line #8
move  unchanged line #9
EOF
	cat <<EOF >dst/move/changed/text &&
move XXXCHANGED line #0
move    changed line #1
move    changed line #2
move    changed line #3
move    changed line #4
move    changed line #5
move    changed line #6
move    changed line #7
move    changed line #8
move    changed line #9
EOF
	cat <<EOF >dst/move/rearranged/text &&
move rearranged line #1
move rearranged line #0
move rearranged line #2
move rearranged line #3
move rearranged line #4
move rearranged line #5
move rearranged line #6
move rearranged line #7
move rearranged line #8
move rearranged line #9
EOF
	git add . &&
	git rm -r src/move/unchanged &&
	git rm -r src/move/changed &&
	git rm -r src/move/rearranged &&
	git commit -m "changes"
'

cat <<EOF >expect_diff_stat
1	1	changed/text
10	0	dst/copy/changed/text
10	0	dst/copy/rearranged/text
10	0	dst/copy/unchanged/text
10	0	dst/move/changed/text
10	0	dst/move/rearranged/text
10	0	dst/move/unchanged/text
1	1	rearranged/text
0	10	src/move/changed/text
0	10	src/move/rearranged/text
0	10	src/move/unchanged/text
EOF

cat <<EOF >expect_diff_stat_M
1	1	changed/text
10	0	dst/copy/changed/text
10	0	dst/copy/rearranged/text
10	0	dst/copy/unchanged/text
1	1	{src => dst}/move/changed/text
1	1	{src => dst}/move/rearranged/text
0	0	{src => dst}/move/unchanged/text
1	1	rearranged/text
EOF

cat <<EOF >expect_diff_stat_CC
1	1	changed/text
1	1	{src => dst}/copy/changed/text
1	1	{src => dst}/copy/rearranged/text
0	0	{src => dst}/copy/unchanged/text
1	1	{src => dst}/move/changed/text
1	1	{src => dst}/move/rearranged/text
0	0	{src => dst}/move/unchanged/text
1	1	rearranged/text
EOF

test_expect_success 'sanity check setup (--numstat)' '
	git diff --numstat HEAD^..HEAD >actual_diff_stat &&
	test_cmp expect_diff_stat actual_diff_stat &&
	git diff --numstat -M HEAD^..HEAD >actual_diff_stat_M &&
	test_cmp expect_diff_stat_M actual_diff_stat_M &&
	git diff --numstat -C -C HEAD^..HEAD >actual_diff_stat_CC &&
	test_cmp expect_diff_stat_CC actual_diff_stat_CC
'

# changed/text and rearranged/text falls below default 3% threshold
cat <<EOF >expect_diff_dirstat
  10.8% dst/copy/changed/
  10.8% dst/copy/rearranged/
  10.8% dst/copy/unchanged/
  10.8% dst/move/changed/
  10.8% dst/move/rearranged/
  10.8% dst/move/unchanged/
  10.8% src/move/changed/
  10.8% src/move/rearranged/
  10.8% src/move/unchanged/
EOF

# rearranged/text falls below default 3% threshold
cat <<EOF >expect_diff_dirstat_M
   5.8% changed/
  29.3% dst/copy/changed/
  29.3% dst/copy/rearranged/
  29.3% dst/copy/unchanged/
   5.8% dst/move/changed/
EOF

# rearranged/text falls below default 3% threshold
cat <<EOF >expect_diff_dirstat_CC
  32.6% changed/
  32.6% dst/copy/changed/
  32.6% dst/move/changed/
EOF

test_expect_success 'various ways to misspell --dirstat' '
	test_must_fail git show --dirstat10 &&
	test_must_fail git show --dirstat10,files &&
	test_must_fail git show -X=20 &&
	test_must_fail git show -X=20,cumulative
'

test_expect_success 'vanilla --dirstat' '
	git diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'vanilla -X' '
	git diff -X HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff -X -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff -X -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'explicit defaults: --dirstat=changes,noncumulative,3' '
	git diff --dirstat=changes,noncumulative,3 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=changes,noncumulative,3 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=changes,noncumulative,3 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'explicit defaults: -Xchanges,noncumulative,3' '
	git diff -Xchanges,noncumulative,3 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff -Xchanges,noncumulative,3 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff -Xchanges,noncumulative,3 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'later options override earlier options:' '
	git diff --dirstat=files,10,cumulative,changes,noncumulative,3 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files,10,cumulative,changes,noncumulative,3 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files,10,cumulative,changes,noncumulative,3 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
	git diff --dirstat=files --dirstat=10 --dirstat=cumulative --dirstat=changes --dirstat=noncumulative -X3 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files --dirstat=10 --dirstat=cumulative --dirstat=changes --dirstat=noncumulative -X3 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files --dirstat=10 --dirstat=cumulative --dirstat=changes --dirstat=noncumulative -X3 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'non-defaults in config overridden by explicit defaults on command line' '
	git -c diff.dirstat=files,cumulative,50 diff --dirstat=changes,noncumulative,3 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=files,cumulative,50 diff --dirstat=changes,noncumulative,3 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=files,cumulative,50 diff --dirstat=changes,noncumulative,3 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
   2.1% changed/
  10.8% dst/copy/changed/
  10.8% dst/copy/rearranged/
  10.8% dst/copy/unchanged/
  10.8% dst/move/changed/
  10.8% dst/move/rearranged/
  10.8% dst/move/unchanged/
   0.0% rearranged/
  10.8% src/move/changed/
  10.8% src/move/rearranged/
  10.8% src/move/unchanged/
EOF

cat <<EOF >expect_diff_dirstat_M
   5.8% changed/
  29.3% dst/copy/changed/
  29.3% dst/copy/rearranged/
  29.3% dst/copy/unchanged/
   5.8% dst/move/changed/
   0.1% dst/move/rearranged/
   0.1% rearranged/
EOF

cat <<EOF >expect_diff_dirstat_CC
  32.6% changed/
  32.6% dst/copy/changed/
   0.6% dst/copy/rearranged/
  32.6% dst/move/changed/
   0.6% dst/move/rearranged/
   0.6% rearranged/
EOF

test_expect_success '--dirstat=0' '
	git diff --dirstat=0 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=0 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=0 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '-X0' '
	git diff -X0 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff -X0 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff -X0 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=0' '
	git -c diff.dirstat=0 diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=0 diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=0 diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
   2.1% changed/
  10.8% dst/copy/changed/
  10.8% dst/copy/rearranged/
  10.8% dst/copy/unchanged/
  32.5% dst/copy/
  10.8% dst/move/changed/
  10.8% dst/move/rearranged/
  10.8% dst/move/unchanged/
  32.5% dst/move/
  65.1% dst/
   0.0% rearranged/
  10.8% src/move/changed/
  10.8% src/move/rearranged/
  10.8% src/move/unchanged/
  32.5% src/move/
EOF

cat <<EOF >expect_diff_dirstat_M
   5.8% changed/
  29.3% dst/copy/changed/
  29.3% dst/copy/rearranged/
  29.3% dst/copy/unchanged/
  88.0% dst/copy/
   5.8% dst/move/changed/
   0.1% dst/move/rearranged/
   5.9% dst/move/
  94.0% dst/
   0.1% rearranged/
EOF

cat <<EOF >expect_diff_dirstat_CC
  32.6% changed/
  32.6% dst/copy/changed/
   0.6% dst/copy/rearranged/
  33.3% dst/copy/
  32.6% dst/move/changed/
   0.6% dst/move/rearranged/
  33.3% dst/move/
  66.6% dst/
   0.6% rearranged/
EOF

test_expect_success '--dirstat=0 --cumulative' '
	git diff --dirstat=0 --cumulative HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=0 --cumulative -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=0 --cumulative -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '--dirstat=0,cumulative' '
	git diff --dirstat=0,cumulative HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=0,cumulative -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=0,cumulative -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '-X0,cumulative' '
	git diff -X0,cumulative HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff -X0,cumulative -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff -X0,cumulative -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=0,cumulative' '
	git -c diff.dirstat=0,cumulative diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=0,cumulative diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=0,cumulative diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=0 & --dirstat=cumulative' '
	git -c diff.dirstat=0 diff --dirstat=cumulative HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=0 diff --dirstat=cumulative -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=0 diff --dirstat=cumulative -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
   9.0% changed/
   9.0% dst/copy/changed/
   9.0% dst/copy/rearranged/
   9.0% dst/copy/unchanged/
   9.0% dst/move/changed/
   9.0% dst/move/rearranged/
   9.0% dst/move/unchanged/
   9.0% rearranged/
   9.0% src/move/changed/
   9.0% src/move/rearranged/
   9.0% src/move/unchanged/
EOF

cat <<EOF >expect_diff_dirstat_M
  14.2% changed/
  14.2% dst/copy/changed/
  14.2% dst/copy/rearranged/
  14.2% dst/copy/unchanged/
  14.2% dst/move/changed/
  14.2% dst/move/rearranged/
  14.2% rearranged/
EOF

cat <<EOF >expect_diff_dirstat_CC
  16.6% changed/
  16.6% dst/copy/changed/
  16.6% dst/copy/rearranged/
  16.6% dst/move/changed/
  16.6% dst/move/rearranged/
  16.6% rearranged/
EOF

test_expect_success '--dirstat-by-file' '
	git diff --dirstat-by-file HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat-by-file -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat-by-file -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '--dirstat=files' '
	git diff --dirstat=files HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=files' '
	git -c diff.dirstat=files diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=files diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=files diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
  27.2% dst/copy/
  27.2% dst/move/
  27.2% src/move/
EOF

cat <<EOF >expect_diff_dirstat_M
  14.2% changed/
  14.2% dst/copy/changed/
  14.2% dst/copy/rearranged/
  14.2% dst/copy/unchanged/
  14.2% dst/move/changed/
  14.2% dst/move/rearranged/
  14.2% rearranged/
EOF

cat <<EOF >expect_diff_dirstat_CC
  16.6% changed/
  16.6% dst/copy/changed/
  16.6% dst/copy/rearranged/
  16.6% dst/move/changed/
  16.6% dst/move/rearranged/
  16.6% rearranged/
EOF

test_expect_success '--dirstat-by-file=10' '
	git diff --dirstat-by-file=10 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat-by-file=10 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat-by-file=10 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '--dirstat=files,10' '
	git diff --dirstat=files,10 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files,10 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files,10 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=10,files' '
	git -c diff.dirstat=10,files diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=10,files diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=10,files diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
   9.0% changed/
   9.0% dst/copy/changed/
   9.0% dst/copy/rearranged/
   9.0% dst/copy/unchanged/
  27.2% dst/copy/
   9.0% dst/move/changed/
   9.0% dst/move/rearranged/
   9.0% dst/move/unchanged/
  27.2% dst/move/
  54.5% dst/
   9.0% rearranged/
   9.0% src/move/changed/
   9.0% src/move/rearranged/
   9.0% src/move/unchanged/
  27.2% src/move/
EOF

cat <<EOF >expect_diff_dirstat_M
  14.2% changed/
  14.2% dst/copy/changed/
  14.2% dst/copy/rearranged/
  14.2% dst/copy/unchanged/
  42.8% dst/copy/
  14.2% dst/move/changed/
  14.2% dst/move/rearranged/
  28.5% dst/move/
  71.4% dst/
  14.2% rearranged/
EOF

cat <<EOF >expect_diff_dirstat_CC
  16.6% changed/
  16.6% dst/copy/changed/
  16.6% dst/copy/rearranged/
  33.3% dst/copy/
  16.6% dst/move/changed/
  16.6% dst/move/rearranged/
  33.3% dst/move/
  66.6% dst/
  16.6% rearranged/
EOF

test_expect_success '--dirstat-by-file --cumulative' '
	git diff --dirstat-by-file --cumulative HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat-by-file --cumulative -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat-by-file --cumulative -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '--dirstat=files,cumulative' '
	git diff --dirstat=files,cumulative HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files,cumulative -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files,cumulative -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=cumulative,files' '
	git -c diff.dirstat=cumulative,files diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=cumulative,files diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=cumulative,files diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
  27.2% dst/copy/
  27.2% dst/move/
  54.5% dst/
  27.2% src/move/
EOF

cat <<EOF >expect_diff_dirstat_M
  14.2% changed/
  14.2% dst/copy/changed/
  14.2% dst/copy/rearranged/
  14.2% dst/copy/unchanged/
  42.8% dst/copy/
  14.2% dst/move/changed/
  14.2% dst/move/rearranged/
  28.5% dst/move/
  71.4% dst/
  14.2% rearranged/
EOF

cat <<EOF >expect_diff_dirstat_CC
  16.6% changed/
  16.6% dst/copy/changed/
  16.6% dst/copy/rearranged/
  33.3% dst/copy/
  16.6% dst/move/changed/
  16.6% dst/move/rearranged/
  33.3% dst/move/
  66.6% dst/
  16.6% rearranged/
EOF

test_expect_success '--dirstat=files,cumulative,10' '
	git diff --dirstat=files,cumulative,10 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files,cumulative,10 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files,cumulative,10 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=10,cumulative,files' '
	git -c diff.dirstat=10,cumulative,files diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=10,cumulative,files diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=10,cumulative,files diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
  27.2% dst/copy/
  27.2% dst/move/
  54.5% dst/
  27.2% src/move/
EOF

cat <<EOF >expect_diff_dirstat_M
  42.8% dst/copy/
  28.5% dst/move/
  71.4% dst/
EOF

cat <<EOF >expect_diff_dirstat_CC
  33.3% dst/copy/
  33.3% dst/move/
  66.6% dst/
EOF

test_expect_success '--dirstat=files,cumulative,16.7' '
	git diff --dirstat=files,cumulative,16.7 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files,cumulative,16.7 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files,cumulative,16.7 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=16.7,cumulative,files' '
	git -c diff.dirstat=16.7,cumulative,files diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=16.7,cumulative,files diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=16.7,cumulative,files diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=16.70,cumulative,files' '
	git -c diff.dirstat=16.70,cumulative,files diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=16.70,cumulative,files diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=16.70,cumulative,files diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '--dirstat=files,cumulative,27.2' '
	git diff --dirstat=files,cumulative,27.2 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files,cumulative,27.2 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files,cumulative,27.2 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '--dirstat=files,cumulative,27.09' '
	git diff --dirstat=files,cumulative,27.09 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=files,cumulative,27.09 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=files,cumulative,27.09 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
  10.6% dst/copy/changed/
  10.6% dst/copy/rearranged/
  10.6% dst/copy/unchanged/
  10.6% dst/move/changed/
  10.6% dst/move/rearranged/
  10.6% dst/move/unchanged/
  10.6% src/move/changed/
  10.6% src/move/rearranged/
  10.6% src/move/unchanged/
EOF

cat <<EOF >expect_diff_dirstat_M
   5.2% changed/
  26.3% dst/copy/changed/
  26.3% dst/copy/rearranged/
  26.3% dst/copy/unchanged/
   5.2% dst/move/changed/
   5.2% dst/move/rearranged/
   5.2% rearranged/
EOF

cat <<EOF >expect_diff_dirstat_CC
  16.6% changed/
  16.6% dst/copy/changed/
  16.6% dst/copy/rearranged/
  16.6% dst/move/changed/
  16.6% dst/move/rearranged/
  16.6% rearranged/
EOF

test_expect_success '--dirstat=lines' '
	git diff --dirstat=lines HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=lines -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=lines -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=lines' '
	git -c diff.dirstat=lines diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=lines diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=lines diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

cat <<EOF >expect_diff_dirstat
   2.1% changed/
  10.6% dst/copy/changed/
  10.6% dst/copy/rearranged/
  10.6% dst/copy/unchanged/
  10.6% dst/move/changed/
  10.6% dst/move/rearranged/
  10.6% dst/move/unchanged/
   2.1% rearranged/
  10.6% src/move/changed/
  10.6% src/move/rearranged/
  10.6% src/move/unchanged/
EOF

cat <<EOF >expect_diff_dirstat_M
   5.2% changed/
  26.3% dst/copy/changed/
  26.3% dst/copy/rearranged/
  26.3% dst/copy/unchanged/
   5.2% dst/move/changed/
   5.2% dst/move/rearranged/
   5.2% rearranged/
EOF

cat <<EOF >expect_diff_dirstat_CC
  16.6% changed/
  16.6% dst/copy/changed/
  16.6% dst/copy/rearranged/
  16.6% dst/move/changed/
  16.6% dst/move/rearranged/
  16.6% rearranged/
EOF

test_expect_success '--dirstat=lines,0' '
	git diff --dirstat=lines,0 HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git diff --dirstat=lines,0 -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git diff --dirstat=lines,0 -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success 'diff.dirstat=0,lines' '
	git -c diff.dirstat=0,lines diff --dirstat HEAD^..HEAD >actual_diff_dirstat &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	git -c diff.dirstat=0,lines diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	git -c diff.dirstat=0,lines diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC
'

test_expect_success '--dirstat=future_param,lines,0 should fail loudly' '
	test_must_fail git diff --dirstat=future_param,lines,0 HEAD^..HEAD >actual_diff_dirstat 2>actual_error &&
	test_debug "cat actual_error" &&
	test_cmp /dev/null actual_diff_dirstat &&
	test_i18ngrep -q "future_param" actual_error &&
	test_i18ngrep -q "\--dirstat" actual_error
'

test_expect_success '--dirstat=dummy1,cumulative,2dummy should report both unrecognized parameters' '
	test_must_fail git diff --dirstat=dummy1,cumulative,2dummy HEAD^..HEAD >actual_diff_dirstat 2>actual_error &&
	test_debug "cat actual_error" &&
	test_cmp /dev/null actual_diff_dirstat &&
	test_i18ngrep -q "dummy1" actual_error &&
	test_i18ngrep -q "2dummy" actual_error &&
	test_i18ngrep -q "\--dirstat" actual_error
'

test_expect_success 'diff.dirstat=future_param,0,lines should warn, but still work' '
	git -c diff.dirstat=future_param,0,lines diff --dirstat HEAD^..HEAD >actual_diff_dirstat 2>actual_error &&
	test_debug "cat actual_error" &&
	test_cmp expect_diff_dirstat actual_diff_dirstat &&
	test_i18ngrep -q "future_param" actual_error &&
	test_i18ngrep -q "diff\\.dirstat" actual_error &&

	git -c diff.dirstat=future_param,0,lines diff --dirstat -M HEAD^..HEAD >actual_diff_dirstat_M 2>actual_error &&
	test_debug "cat actual_error" &&
	test_cmp expect_diff_dirstat_M actual_diff_dirstat_M &&
	test_i18ngrep -q "future_param" actual_error &&
	test_i18ngrep -q "diff\\.dirstat" actual_error &&

	git -c diff.dirstat=future_param,0,lines diff --dirstat -C -C HEAD^..HEAD >actual_diff_dirstat_CC 2>actual_error &&
	test_debug "cat actual_error" &&
	test_cmp expect_diff_dirstat_CC actual_diff_dirstat_CC &&
	test_i18ngrep -q "future_param" actual_error &&
	test_i18ngrep -q "diff\\.dirstat" actual_error
'

test_done
