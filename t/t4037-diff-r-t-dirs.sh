#!/bin/sh

test_description='diff -r -t shows directory additions and deletions'

. ./test-lib.sh

test_expect_success setup '
	mkdir dc dr dt &&
	>dc/1 &&
	>dr/2 &&
	>dt/3 &&
	>fc &&
	>fr &&
	>ft &&
	git add . &&
	test_tick &&
	git commit -m initial &&

	rm -fr dt dr ft fr &&
	mkdir da ft &&
	for p in dc/1 da/4 dt ft/5 fc
	do
		echo hello >$p || exit
	done &&
	git add -u &&
	git add . &&
	test_tick &&
	git commit -m second
'

cat >expect <<\EOF
A	da
A	da/4
M	dc
M	dc/1
D	dr
D	dr/2
A	dt
D	dt
D	dt/3
M	fc
D	fr
D	ft
A	ft
A	ft/5
EOF

test_expect_success verify '
	git diff-tree -r -t --name-status HEAD^ HEAD >actual &&
	test_cmp expect actual
'

test_done
