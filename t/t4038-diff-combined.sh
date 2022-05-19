#!/bin/sh

test_description='combined diff'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

setup_helper () {
	one=$1 branch=$2 side=$3 &&

	but branch $side $branch &&
	for l in $one two three fyra
	do
		echo $l
	done >file &&
	but add file &&
	test_tick &&
	but cummit -m $branch &&
	but checkout $side &&
	for l in $one two three quatro
	do
		echo $l
	done >file &&
	but add file &&
	test_tick &&
	but cummit -m $side &&
	test_must_fail but merge $branch &&
	for l in $one three four
	do
		echo $l
	done >file &&
	but add file &&
	test_tick &&
	but cummit -m "merge $branch into $side"
}

verify_helper () {
	it=$1 &&

	# Ignore lines that were removed only from the other parent
	sed -e '
		1,/^@@@/d
		/^ -/d
		s/^\(.\)./\1/
	' "$it" >"$it.actual.1" &&
	sed -e '
		1,/^@@@/d
		/^- /d
		s/^.\(.\)/\1/
	' "$it" >"$it.actual.2" &&

	but diff "$it^" "$it" -- | sed -e '1,/^@@/d' >"$it.expect.1" &&
	test_cmp "$it.expect.1" "$it.actual.1" &&

	but diff "$it^2" "$it" -- | sed -e '1,/^@@/d' >"$it.expect.2" &&
	test_cmp "$it.expect.2" "$it.actual.2"
}

test_expect_success setup '
	>file &&
	but add file &&
	test_tick &&
	but cummit -m initial &&

	but branch withone &&
	but branch sansone &&

	but checkout withone &&
	setup_helper one withone sidewithone &&

	but checkout sansone &&
	setup_helper "" sansone sidesansone
'

test_expect_success 'check combined output (1)' '
	but show sidewithone -- >sidewithone &&
	verify_helper sidewithone
'

test_expect_success 'check combined output (2)' '
	but show sidesansone -- >sidesansone &&
	verify_helper sidesansone
'

test_expect_success 'diagnose truncated file' '
	>file &&
	but add file &&
	but cummit --amend -C HEAD &&
	but show >out &&
	grep "diff --cc file" out
'

test_expect_success 'setup for --cc --raw' '
	blob=$(echo file | but hash-object --stdin -w) &&
	base_tree=$(echo "100644 blob $blob	file" | but mktree) &&
	trees= &&
	for i in $(test_seq 1 40)
	do
		blob=$(echo file$i | but hash-object --stdin -w) &&
		trees="$trees$(echo "100644 blob $blob	file" | but mktree)$LF" || return 1
	done
'

test_expect_success 'check --cc --raw with four trees' '
	four_trees=$(echo "$trees" | sed -e 4q) &&
	but diff --cc --raw $four_trees $base_tree >out &&
	# Check for four leading colons in the output:
	grep "^::::[^:]" out
'

test_expect_success 'check --cc --raw with forty trees' '
	but diff --cc --raw $trees $base_tree >out &&
	# Check for forty leading colons in the output:
	grep "^::::::::::::::::::::::::::::::::::::::::[^:]" out
'

test_expect_success 'setup combined ignore spaces' '
	but checkout main &&
	>test &&
	but add test &&
	but cummit -m initial &&

	tr -d Q <<-\EOF >test &&
	always coalesce
	eol space coalesce Q
	space  change coalesce
	all spa ces coalesce
	eol spaces Q
	space  change
	all spa ces
	EOF
	but cummit -m "test space change" -a &&

	but checkout -b side HEAD^ &&
	tr -d Q <<-\EOF >test &&
	always coalesce
	eol space coalesce
	space change coalesce
	all spaces coalesce
	eol spaces
	space change
	all spaces
	EOF
	but cummit -m "test other space changes" -a &&

	test_must_fail but merge main &&
	tr -d Q <<-\EOF >test &&
	eol spaces Q
	space  change
	all spa ces
	EOF
	but cummit -m merged -a
'

test_expect_success 'check combined output (no ignore space)' '
	but show >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	--always coalesce
	- eol space coalesce
	- space change coalesce
	- all spaces coalesce
	- eol spaces
	- space change
	- all spaces
	 -eol space coalesce Q
	 -space  change coalesce
	 -all spa ces coalesce
	+ eol spaces Q
	+ space  change
	+ all spa ces
	EOF
	compare_diff_patch expected actual
'

test_expect_success 'check combined output (ignore space at eol)' '
	but show --ignore-space-at-eol >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	--always coalesce
	--eol space coalesce
	- space change coalesce
	- all spaces coalesce
	 -space  change coalesce
	 -all spa ces coalesce
	  eol spaces Q
	- space change
	- all spaces
	+ space  change
	+ all spa ces
	EOF
	compare_diff_patch expected actual
'

test_expect_success 'check combined output (ignore space change)' '
	but show -b >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	--always coalesce
	--eol space coalesce
	--space change coalesce
	- all spaces coalesce
	 -all spa ces coalesce
	  eol spaces Q
	  space  change
	- all spaces
	+ all spa ces
	EOF
	compare_diff_patch expected actual
'

test_expect_success 'check combined output (ignore all spaces)' '
	but show -w >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	--always coalesce
	--eol space coalesce
	--space change coalesce
	--all spaces coalesce
	  eol spaces Q
	  space  change
	  all spa ces
	EOF
	compare_diff_patch expected actual
'

test_expect_success 'combine diff coalesce simple' '
	>test &&
	but add test &&
	but cummit -m initial &&
	test_seq 4 >test &&
	but cummit -a -m empty1 &&
	but branch side1 &&
	but checkout HEAD^ &&
	test_seq 5 >test &&
	but cummit -a -m empty2 &&
	test_must_fail but merge side1 &&
	>test &&
	but cummit -a -m merge &&
	but show >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	--1
	--2
	--3
	--4
	- 5
	EOF
	compare_diff_patch expected actual
'

test_expect_success 'combine diff coalesce tricky' '
	>test &&
	but add test &&
	but cummit -m initial --allow-empty &&
	cat <<-\EOF >test &&
	3
	1
	2
	3
	4
	EOF
	but cummit -a -m empty1 &&
	but branch -f side1 &&
	but checkout HEAD^ &&
	cat <<-\EOF >test &&
	1
	3
	5
	4
	EOF
	but cummit -a -m empty2 &&
	but branch -f side2 &&
	test_must_fail but merge side1 &&
	>test &&
	but cummit -a -m merge &&
	but show >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	 -3
	--1
	 -2
	--3
	- 5
	--4
	EOF
	compare_diff_patch expected actual &&
	but checkout -f side1 &&
	test_must_fail but merge side2 &&
	>test &&
	but cummit -a -m merge &&
	but show >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	- 3
	--1
	- 2
	--3
	 -5
	--4
	EOF
	compare_diff_patch expected actual
'

test_expect_failure 'combine diff coalesce three parents' '
	>test &&
	but add test &&
	but cummit -m initial --allow-empty &&
	cat <<-\EOF >test &&
	3
	1
	2
	3
	4
	EOF
	but cummit -a -m empty1 &&
	but checkout -B side1 &&
	but checkout HEAD^ &&
	cat <<-\EOF >test &&
	1
	3
	7
	5
	4
	EOF
	but cummit -a -m empty2 &&
	but branch -f side2 &&
	but checkout HEAD^ &&
	cat <<-\EOF >test &&
	3
	1
	6
	5
	4
	EOF
	but cummit -a -m empty3 &&
	>test &&
	but add test &&
	TREE=$(but write-tree) &&
	cummit=$(but cummit-tree -p HEAD -p side1 -p side2 -m merge $TREE) &&
	but show $cummit >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	-- 3
	---1
	-  6
	 - 2
	 --3
	  -7
	- -5
	---4
	EOF
	compare_diff_patch expected actual
'

# Test for a bug reported at
# https://lore.kernel.org/but/20130515143508.GO25742@login.drsnuggles.stderr.nl/
# where a delete lines were missing from combined diff output when they
# occurred exactly before the context lines of a later change.
test_expect_success 'combine diff missing delete bug' '
	but cummit -m initial --allow-empty &&
	cat <<-\EOF >test &&
	1
	2
	3
	4
	EOF
	but add test &&
	but cummit -a -m side1 &&
	but checkout -B side1 &&
	but checkout HEAD^ &&
	cat <<-\EOF >test &&
	0
	1
	2
	3
	4modified
	EOF
	but add test &&
	but cummit -m side2 &&
	but branch -f side2 &&
	test_must_fail but merge --no-cummit side1 &&
	cat <<-\EOF >test &&
	1
	2
	3
	4modified
	EOF
	but add test &&
	but cummit -a -m merge &&
	but diff-tree -c -p HEAD >actual.tmp &&
	sed -e "1,/^@@@/d" < actual.tmp >actual &&
	tr -d Q <<-\EOF >expected &&
	- 0
	  1
	  2
	  3
	 -4
	 +4modified
	EOF
	compare_diff_patch expected actual
'

test_expect_success 'combine diff gets tree sorting right' '
	# create a directory and a file that sort differently in trees
	# versus byte-wise (implied "/" sorts after ".")
	but checkout -f main &&
	mkdir foo &&
	echo base >foo/one &&
	echo base >foo/two &&
	echo base >foo.ext &&
	but add foo foo.ext &&
	but cummit -m base &&

	# one side modifies a file in the directory, along with the root
	# file...
	echo main >foo/one &&
	echo main >foo.ext &&
	but cummit -a -m main &&

	# the other side modifies the other file in the directory
	but checkout -b other HEAD^ &&
	echo other >foo/two &&
	but cummit -a -m other &&

	# And now we merge. The files in the subdirectory will resolve cleanly,
	# meaning that a combined diff will not find them interesting. But it
	# will find the tree itself interesting, because it had to be merged.
	but checkout main &&
	but merge other &&

	printf "MM\tfoo\n" >expect &&
	but diff-tree -c --name-status -t HEAD >actual.tmp &&
	sed 1d <actual.tmp >actual &&
	test_cmp expect actual
'

test_expect_success 'setup for --combined-all-paths' '
	but branch side1c &&
	but branch side2c &&
	but checkout side1c &&
	test_seq 1 10 >filename-side1c &&
	side1cf=$(but hash-object filename-side1c) &&
	but add filename-side1c &&
	but cummit -m with &&
	but checkout side2c &&
	test_seq 1 9 >filename-side2c &&
	echo ten >>filename-side2c &&
	side2cf=$(but hash-object filename-side2c) &&
	but add filename-side2c &&
	but cummit -m iam &&
	but checkout -b mergery side1c &&
	but merge --no-cummit side2c &&
	but rm filename-side1c &&
	echo eleven >>filename-side2c &&
	but mv filename-side2c filename-merged &&
	mergedf=$(but hash-object filename-merged) &&
	but add filename-merged &&
	but cummit
'

test_expect_success '--combined-all-paths and --raw' '
	cat <<-EOF >expect &&
	::100644 100644 100644 $side1cf $side2cf $mergedf RR	filename-side1c	filename-side2c	filename-merged
	EOF
	but diff-tree -c -M --raw --combined-all-paths HEAD >actual.tmp &&
	sed 1d <actual.tmp >actual &&
	test_cmp expect actual
'

test_expect_success '--combined-all-paths and --cc' '
	cat <<-\EOF >expect &&
	--- a/filename-side1c
	--- a/filename-side2c
	+++ b/filename-merged
	EOF
	but diff-tree --cc -M --combined-all-paths HEAD >actual.tmp &&
	grep ^[-+][-+][-+] <actual.tmp >actual &&
	test_cmp expect actual
'

test_expect_success FUNNYNAMES 'setup for --combined-all-paths with funny names' '
	but branch side1d &&
	but branch side2d &&
	but checkout side1d &&
	test_seq 1 10 >"$(printf "file\twith\ttabs")" &&
	but add file* &&
	side1df=$(but hash-object *tabs) &&
	but cummit -m with &&
	but checkout side2d &&
	test_seq 1 9 >"$(printf "i\tam\ttabbed")" &&
	echo ten >>"$(printf "i\tam\ttabbed")" &&
	but add *tabbed &&
	side2df=$(but hash-object *tabbed) &&
	but cummit -m iam &&
	but checkout -b funny-names-mergery side1d &&
	but merge --no-cummit side2d &&
	but rm *tabs &&
	echo eleven >>"$(printf "i\tam\ttabbed")" &&
	but mv "$(printf "i\tam\ttabbed")" "$(printf "fickle\tnaming")" &&
	but add fickle* &&
	headf=$(but hash-object fickle*) &&
	but cummit &&
	head=$(but rev-parse HEAD)
'

test_expect_success FUNNYNAMES '--combined-all-paths and --raw and funny names' '
	cat <<-EOF >expect &&
	::100644 100644 100644 $side1df $side2df $headf RR	"file\twith\ttabs"	"i\tam\ttabbed"	"fickle\tnaming"
	EOF
	but diff-tree -c -M --raw --combined-all-paths HEAD >actual.tmp &&
	sed 1d <actual.tmp >actual &&
	test_cmp expect actual
'

test_expect_success FUNNYNAMES '--combined-all-paths and --raw -and -z and funny names' '
	printf "$head\0::100644 100644 100644 $side1df $side2df $headf RR\0file\twith\ttabs\0i\tam\ttabbed\0fickle\tnaming\0" >expect &&
	but diff-tree -c -M --raw --combined-all-paths -z HEAD >actual &&
	test_cmp expect actual
'

test_expect_success FUNNYNAMES '--combined-all-paths and --cc and funny names' '
	cat <<-\EOF >expect &&
	--- "a/file\twith\ttabs"
	--- "a/i\tam\ttabbed"
	+++ "b/fickle\tnaming"
	EOF
	but diff-tree --cc -M --combined-all-paths HEAD >actual.tmp &&
	grep ^[-+][-+][-+] <actual.tmp >actual &&
	test_cmp expect actual
'

test_done
