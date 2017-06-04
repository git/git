#!/bin/sh

test_description='combined diff'

. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

setup_helper () {
	one=$1 branch=$2 side=$3 &&

	git branch $side $branch &&
	for l in $one two three fyra
	do
		echo $l
	done >file &&
	git add file &&
	test_tick &&
	git commit -m $branch &&
	git checkout $side &&
	for l in $one two three quatro
	do
		echo $l
	done >file &&
	git add file &&
	test_tick &&
	git commit -m $side &&
	test_must_fail git merge $branch &&
	for l in $one three four
	do
		echo $l
	done >file &&
	git add file &&
	test_tick &&
	git commit -m "merge $branch into $side"
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

	git diff "$it^" "$it" -- | sed -e '1,/^@@/d' >"$it.expect.1" &&
	test_cmp "$it.expect.1" "$it.actual.1" &&

	git diff "$it^2" "$it" -- | sed -e '1,/^@@/d' >"$it.expect.2" &&
	test_cmp "$it.expect.2" "$it.actual.2"
}

test_expect_success setup '
	>file &&
	git add file &&
	test_tick &&
	git commit -m initial &&

	git branch withone &&
	git branch sansone &&

	git checkout withone &&
	setup_helper one withone sidewithone &&

	git checkout sansone &&
	setup_helper "" sansone sidesansone
'

test_expect_success 'check combined output (1)' '
	git show sidewithone -- >sidewithone &&
	verify_helper sidewithone
'

test_expect_success 'check combined output (2)' '
	git show sidesansone -- >sidesansone &&
	verify_helper sidesansone
'

test_expect_success 'diagnose truncated file' '
	>file &&
	git add file &&
	git commit --amend -C HEAD &&
	git show >out &&
	grep "diff --cc file" out
'

test_expect_success 'setup for --cc --raw' '
	blob=$(echo file | git hash-object --stdin -w) &&
	base_tree=$(echo "100644 blob $blob	file" | git mktree) &&
	trees= &&
	for i in $(test_seq 1 40)
	do
		blob=$(echo file$i | git hash-object --stdin -w) &&
		trees="$trees$(echo "100644 blob $blob	file" | git mktree)$LF"
	done
'

test_expect_success 'check --cc --raw with four trees' '
	four_trees=$(echo "$trees" | sed -e 4q) &&
	git diff --cc --raw $four_trees $base_tree >out &&
	# Check for four leading colons in the output:
	grep "^::::[^:]" out
'

test_expect_success 'check --cc --raw with forty trees' '
	git diff --cc --raw $trees $base_tree >out &&
	# Check for forty leading colons in the output:
	grep "^::::::::::::::::::::::::::::::::::::::::[^:]" out
'

test_expect_success 'setup combined ignore spaces' '
	git checkout master &&
	>test &&
	git add test &&
	git commit -m initial &&

	tr -d Q <<-\EOF >test &&
	always coalesce
	eol space coalesce Q
	space  change coalesce
	all spa ces coalesce
	eol spaces Q
	space  change
	all spa ces
	EOF
	git commit -m "test space change" -a &&

	git checkout -b side HEAD^ &&
	tr -d Q <<-\EOF >test &&
	always coalesce
	eol space coalesce
	space change coalesce
	all spaces coalesce
	eol spaces
	space change
	all spaces
	EOF
	git commit -m "test other space changes" -a &&

	test_must_fail git merge master &&
	tr -d Q <<-\EOF >test &&
	eol spaces Q
	space  change
	all spa ces
	EOF
	git commit -m merged -a
'

test_expect_success 'check combined output (no ignore space)' '
	git show >actual.tmp &&
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
	git show --ignore-space-at-eol >actual.tmp &&
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
	git show -b >actual.tmp &&
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
	git show -w >actual.tmp &&
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
	git add test &&
	git commit -m initial &&
	test_seq 4 >test &&
	git commit -a -m empty1 &&
	git branch side1 &&
	git checkout HEAD^ &&
	test_seq 5 >test &&
	git commit -a -m empty2 &&
	test_must_fail git merge side1 &&
	>test &&
	git commit -a -m merge &&
	git show >actual.tmp &&
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
	git add test &&
	git commit -m initial --allow-empty &&
	cat <<-\EOF >test &&
	3
	1
	2
	3
	4
	EOF
	git commit -a -m empty1 &&
	git branch -f side1 &&
	git checkout HEAD^ &&
	cat <<-\EOF >test &&
	1
	3
	5
	4
	EOF
	git commit -a -m empty2 &&
	git branch -f side2 &&
	test_must_fail git merge side1 &&
	>test &&
	git commit -a -m merge &&
	git show >actual.tmp &&
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
	git checkout -f side1 &&
	test_must_fail git merge side2 &&
	>test &&
	git commit -a -m merge &&
	git show >actual.tmp &&
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
	git add test &&
	git commit -m initial --allow-empty &&
	cat <<-\EOF >test &&
	3
	1
	2
	3
	4
	EOF
	git commit -a -m empty1 &&
	git checkout -B side1 &&
	git checkout HEAD^ &&
	cat <<-\EOF >test &&
	1
	3
	7
	5
	4
	EOF
	git commit -a -m empty2 &&
	git branch -f side2 &&
	git checkout HEAD^ &&
	cat <<-\EOF >test &&
	3
	1
	6
	5
	4
	EOF
	git commit -a -m empty3 &&
	>test &&
	git add test &&
	TREE=$(git write-tree) &&
	COMMIT=$(git commit-tree -p HEAD -p side1 -p side2 -m merge $TREE) &&
	git show $COMMIT >actual.tmp &&
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
# https://public-inbox.org/git/20130515143508.GO25742@login.drsnuggles.stderr.nl/
# where a delete lines were missing from combined diff output when they
# occurred exactly before the context lines of a later change.
test_expect_success 'combine diff missing delete bug' '
	git commit -m initial --allow-empty &&
	cat <<-\EOF >test &&
	1
	2
	3
	4
	EOF
	git add test &&
	git commit -a -m side1 &&
	git checkout -B side1 &&
	git checkout HEAD^ &&
	cat <<-\EOF >test &&
	0
	1
	2
	3
	4modified
	EOF
	git add test &&
	git commit -m side2 &&
	git branch -f side2 &&
	test_must_fail git merge --no-commit side1 &&
	cat <<-\EOF >test &&
	1
	2
	3
	4modified
	EOF
	git add test &&
	git commit -a -m merge &&
	git diff-tree -c -p HEAD >actual.tmp &&
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
	git checkout -f master &&
	mkdir foo &&
	echo base >foo/one &&
	echo base >foo/two &&
	echo base >foo.ext &&
	git add foo foo.ext &&
	git commit -m base &&

	# one side modifies a file in the directory, along with the root
	# file...
	echo master >foo/one &&
	echo master >foo.ext &&
	git commit -a -m master &&

	# the other side modifies the other file in the directory
	git checkout -b other HEAD^ &&
	echo other >foo/two &&
	git commit -a -m other &&

	# And now we merge. The files in the subdirectory will resolve cleanly,
	# meaning that a combined diff will not find them interesting. But it
	# will find the tree itself interesting, because it had to be merged.
	git checkout master &&
	git merge other &&

	printf "MM\tfoo\n" >expect &&
	git diff-tree -c --name-status -t HEAD >actual.tmp &&
	sed 1d <actual.tmp >actual &&
	test_cmp expect actual
'

test_done
