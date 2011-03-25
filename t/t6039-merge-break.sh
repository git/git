#!/bin/sh

test_description='merging with renames from broken pairs

This is based on a real-world practice of moving a header file to a
new location, but installing a "replacement" file that points to
the old one. We need break detection in the merge to find the
rename.
'
. ./test-lib.sh

# A fake header file; it needs a fair bit of content
# for break detection and inexact rename detection to work.
mksample() {
	echo '#ifndef SAMPLE_H'
	echo '#define SAMPLE_H'
	for i in 0 1 2 3 4; do
		for j in 0 1 2 3 4 5 6 7 8 9; do
			echo "extern fun$i$j();"
		done
	done
	echo '#endif /* SAMPLE_H */'
}

mvsample() {
	sed 's/SAMPLE_H/NEW_H/' "$1" >"$2" &&
	rm "$1"
}

# A replacement sample header file that references a new one.
mkreplacement() {
	echo '#ifndef SAMPLE_H'
	echo '#define SAMPLE_H'
	echo "#include \"$1\""
	echo '#endif /* SAMPLE_H */'
}

# Tweak the header file in a minor way.
tweak() {
	sed 's,42.*,& /* secret of something-or-other */,' "$1" >"$1.tmp" &&
	mv "$1.tmp" "$1"
}

reset() {
	git reset --hard &&
	git checkout master &&
	git reset --hard base &&
	git clean -f &&
	{ git branch -D topic || true; }
}

test_expect_success 'setup baseline' '
	mksample >sample.h &&
	git add sample.h &&
	git commit -m "add sample.h" &&
	git tag base
'

setup_rename_plus_tweak() {
	reset &&
	mvsample sample.h new.h &&
	mkreplacement new.h >sample.h &&
	git add sample.h new.h &&
	git commit -m 'rename sample.h to new.h, with replacement' &&
	git checkout -b topic base &&
	tweak sample.h &&
	git commit -a -m 'tweak sample.h'
}

check_tweak_result() {
	mksample >expect.orig &&
	mvsample expect.orig expect &&
	tweak expect &&
	test_cmp expect new.h &&
	mkreplacement new.h >expect &&
	test_cmp expect sample.h
}

test_expect_success 'merge rename to tweak finds rename' '
	setup_rename_plus_tweak &&
	git merge master &&
	check_tweak_result
'

test_expect_success 'merge tweak to rename finds rename' '
	setup_rename_plus_tweak &&
	git checkout master &&
	git merge topic &&
	check_tweak_result
'

setup_double_rename_one_replacement() {
	setup_rename_plus_tweak &&
	mvsample sample.h new.h &&
	git add new.h &&
	git commit -a -m 'rename sample.h to new.h (no replacement)'
}

test_expect_success 'merge rename to rename/tweak (one replacement)' '
	setup_double_rename_one_replacement &&
	git merge master &&
	check_tweak_result
'

test_expect_success 'merge rename/tweak to rename (one replacement)' '
	setup_double_rename_one_replacement &&
	git checkout master &&
	git merge topic &&
	check_tweak_result
'

setup_double_rename_two_replacements_same() {
	setup_rename_plus_tweak &&
	mvsample sample.h new.h &&
	mkreplacement new.h >sample.h &&
	git add sample.h new.h &&
	git commit -m 'rename sample.h to new.h with replacement (same)'
}

test_expect_success 'merge rename to rename/tweak (two replacements, same)' '
	setup_double_rename_two_replacements_same &&
	git merge master &&
	check_tweak_result
'

test_expect_success 'merge rename/tweak to rename (two replacements, same)' '
	setup_double_rename_two_replacements_same &&
	git checkout master &&
	git merge topic &&
	check_tweak_result
'

setup_double_rename_two_replacements_diff() {
	setup_rename_plus_tweak &&
	mvsample sample.h new.h &&
	mkreplacement diff.h >sample.h &&
	git add sample.h new.h &&
	git commit -m 'rename sample.h to new.h with replacement (diff)'
}

test_expect_success 'merge rename to rename/tweak (two replacements, diff)' '
	setup_double_rename_two_replacements_diff &&
	test_must_fail git merge master &&
	cat >expect <<-\EOF &&
	#ifndef SAMPLE_H
	#define SAMPLE_H
	<<<<<<< HEAD
	#include "diff.h"
	=======
	#include "new.h"
	>>>>>>> master
	#endif /* SAMPLE_H */
	EOF
	test_cmp expect sample.h
'

test_expect_success 'merge rename to rename/tweak (two replacements, diff)' '
	setup_double_rename_two_replacements_diff &&
	git checkout master &&
	test_must_fail git merge topic &&
	cat >expect <<-\EOF &&
	#ifndef SAMPLE_H
	#define SAMPLE_H
	<<<<<<< HEAD
	#include "new.h"
	=======
	#include "diff.h"
	>>>>>>> topic
	#endif /* SAMPLE_H */
	EOF
	test_cmp expect sample.h
'

test_done
