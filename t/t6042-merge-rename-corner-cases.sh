#!/bin/sh

test_description="recursive merge corner cases w/ renames but not criss-crosses"
# t6036 has corner cases that involve both criss-cross merges and renames

. ./test-lib.sh

test_expect_success 'setup rename/delete + untracked file' '
	echo "A pretty inscription" >ring &&
	git add ring &&
	test_tick &&
	git commit -m beginning &&

	git branch people &&
	git checkout -b rename-the-ring &&
	git mv ring one-ring-to-rule-them-all &&
	test_tick &&
	git commit -m fullname &&

	git checkout people &&
	git rm ring &&
	echo gollum >owner &&
	git add owner &&
	test_tick &&
	git commit -m track-people-instead-of-objects &&
	echo "Myyy PRECIOUSSS" >ring
'

test_expect_failure "Does git preserve Gollum's precious artifact?" '
	test_must_fail git merge -s recursive rename-the-ring &&

	# Make sure git did not delete an untracked file
	test -f ring
'

# Testcase setup for rename/modify/add-source:
#   Commit A: new file: a
#   Commit B: modify a slightly
#   Commit C: rename a->b, add completely different a
#
# We should be able to merge B & C cleanly

test_expect_success 'setup rename/modify/add-source conflict' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	printf "1\n2\n3\n4\n5\n6\n7\n" >a &&
	git add a &&
	git commit -m A &&
	git tag A &&

	git checkout -b B A &&
	echo 8 >>a &&
	git add a &&
	git commit -m B &&

	git checkout -b C A &&
	git mv a b &&
	echo something completely different >a &&
	git add a &&
	git commit -m C
'

test_expect_failure 'rename/modify/add-source conflict resolvable' '
	git checkout B^0 &&

	git merge -s recursive C^0 &&

	test $(git rev-parse B:a) = $(git rev-parse b) &&
	test $(git rev-parse C:a) = $(git rev-parse a)
'

test_expect_success 'setup resolvable conflict missed if rename missed' '
	git rm -rf . &&
	git clean -fdqx &&
	rm -rf .git &&
	git init &&

	printf "1\n2\n3\n4\n5\n" >a &&
	echo foo >b &&
	git add a b &&
	git commit -m A &&
	git tag A &&

	git checkout -b B A &&
	git mv a c &&
	echo "Completely different content" >a &&
	git add a &&
	git commit -m B &&

	git checkout -b C A &&
	echo 6 >>a &&
	git add a &&
	git commit -m C
'

test_expect_failure 'conflict caused if rename not detected' '
	git checkout -q C^0 &&
	git merge -s recursive B^0 &&

	test 3 -eq $(git ls-files -s | wc -l) &&
	test 0 -eq $(git ls-files -u | wc -l) &&
	test 0 -eq $(git ls-files -o | wc -l) &&

	test 6 -eq $(wc -l < c) &&
	test $(git rev-parse HEAD:a) = $(git rev-parse B:a) &&
	test $(git rev-parse HEAD:b) = $(git rev-parse A:b)
'

test_expect_success 'setup conflict resolved wrong if rename missed' '
	git reset --hard &&
	git clean -f &&

	git checkout -b D A &&
	echo 7 >>a &&
	git add a &&
	git mv a c &&
	echo "Completely different content" >a &&
	git add a &&
	git commit -m D &&

	git checkout -b E A &&
	git rm a &&
	echo "Completely different content" >>a &&
	git add a &&
	git commit -m E
'

test_expect_failure 'missed conflict if rename not detected' '
	git checkout -q E^0 &&
	test_must_fail git merge -s recursive D^0
'

test_done
