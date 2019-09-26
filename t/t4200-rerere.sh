#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin
#

test_description='git rerere

! [fifth] version1
 ! [first] first
  ! [fourth] version1
   ! [master] initial
    ! [second] prefer first over second
     ! [third] version2
------
     + [third] version2
+      [fifth] version1
  +    [fourth] version1
+ +  + [third^] third
    -  [second] prefer first over second
 +  +  [first] first
    +  [second^] second
++++++ [master] initial
'

. ./test-lib.sh

test_expect_success 'setup' '
	cat >a1 <<-\EOF &&
	Some title
	==========
	Whether '\''tis nobler in the mind to suffer
	The slings and arrows of outrageous fortune,
	Or to take arms against a sea of troubles,
	And by opposing end them? To die: to sleep;
	No more; and by a sleep to say we end
	The heart-ache and the thousand natural shocks
	That flesh is heir to, '\''tis a consummation
	Devoutly to be wish'\''d.
	EOF

	git add a1 &&
	test_tick &&
	git commit -q -a -m initial &&

	cat >>a1 <<-\EOF &&
	Some title
	==========
	To die, to sleep;
	To sleep: perchance to dream: ay, there'\''s the rub;
	For in that sleep of death what dreams may come
	When we have shuffled off this mortal coil,
	Must give us pause: there'\''s the respect
	That makes calamity of so long life;
	EOF

	git checkout -b first &&
	test_tick &&
	git commit -q -a -m first &&

	git checkout -b second master &&
	git show first:a1 |
	sed -e "s/To die, t/To die! T/" -e "s/Some title/Some Title/" >a1 &&
	echo "* END *" >>a1 &&
	test_tick &&
	git commit -q -a -m second
'

test_expect_success 'nothing recorded without rerere' '
	rm -rf .git/rr-cache &&
	git config rerere.enabled false &&
	test_must_fail git merge first &&
	! test -d .git/rr-cache
'

test_expect_success 'activate rerere, old style (conflicting merge)' '
	git reset --hard &&
	mkdir .git/rr-cache &&
	test_might_fail git config --unset rerere.enabled &&
	test_must_fail git merge first &&

	sha1=$(perl -pe "s/	.*//" .git/MERGE_RR) &&
	rr=.git/rr-cache/$sha1 &&
	grep "^=======\$" $rr/preimage &&
	! test -f $rr/postimage &&
	! test -f $rr/thisimage
'

test_expect_success 'rerere.enabled works, too' '
	rm -rf .git/rr-cache &&
	git config rerere.enabled true &&
	git reset --hard &&
	test_must_fail git merge first &&

	sha1=$(perl -pe "s/	.*//" .git/MERGE_RR) &&
	rr=.git/rr-cache/$sha1 &&
	grep ^=======$ $rr/preimage
'

test_expect_success 'set up rr-cache' '
	rm -rf .git/rr-cache &&
	git config rerere.enabled true &&
	git reset --hard &&
	test_must_fail git merge first &&
	sha1=$(perl -pe "s/	.*//" .git/MERGE_RR) &&
	rr=.git/rr-cache/$sha1
'

test_expect_success 'rr-cache looks sane' '
	# no postimage or thisimage yet
	! test -f $rr/postimage &&
	! test -f $rr/thisimage &&

	# preimage has right number of lines
	cnt=$(sed -ne "/^<<<<<<</,/^>>>>>>>/p" $rr/preimage | wc -l) &&
	echo $cnt &&
	test $cnt = 13
'

test_expect_success 'rerere diff' '
	git show first:a1 >a1 &&
	cat >expect <<-\EOF &&
	--- a/a1
	+++ b/a1
	@@ -1,4 +1,4 @@
	-Some Title
	+Some title
	 ==========
	 Whether '\''tis nobler in the mind to suffer
	 The slings and arrows of outrageous fortune,
	@@ -8,21 +8,11 @@
	 The heart-ache and the thousand natural shocks
	 That flesh is heir to, '\''tis a consummation
	 Devoutly to be wish'\''d.
	-<<<<<<<
	-Some Title
	-==========
	-To die! To sleep;
	-=======
	 Some title
	 ==========
	 To die, to sleep;
	->>>>>>>
	 To sleep: perchance to dream: ay, there'\''s the rub;
	 For in that sleep of death what dreams may come
	 When we have shuffled off this mortal coil,
	 Must give us pause: there'\''s the respect
	 That makes calamity of so long life;
	-<<<<<<<
	-=======
	-* END *
	->>>>>>>
	EOF
	git rerere diff >out &&
	test_cmp expect out
'

test_expect_success 'rerere status' '
	echo a1 >expect &&
	git rerere status >out &&
	test_cmp expect out
'

test_expect_success 'first postimage wins' '
	git show first:a1 | sed "s/To die: t/To die! T/" >expect &&

	git commit -q -a -m "prefer first over second" &&
	test -f $rr/postimage &&

	oldmtimepost=$(test-tool chmtime --get -60 $rr/postimage) &&

	git checkout -b third master &&
	git show second^:a1 | sed "s/To die: t/To die! T/" >a1 &&
	git commit -q -a -m third &&

	test_must_fail git merge first &&
	# rerere kicked in
	! grep "^=======\$" a1 &&
	test_cmp expect a1
'

test_expect_success 'rerere updates postimage timestamp' '
	newmtimepost=$(test-tool chmtime --get $rr/postimage) &&
	test $oldmtimepost -lt $newmtimepost
'

test_expect_success 'rerere clear' '
	mv $rr/postimage .git/post-saved &&
	echo "$sha1	a1" | perl -pe "y/\012/\000/" >.git/MERGE_RR &&
	git rerere clear &&
	! test -d $rr
'

test_expect_success 'leftover directory' '
	git reset --hard &&
	mkdir -p $rr &&
	test_must_fail git merge first &&
	test -f $rr/preimage
'

test_expect_success 'missing preimage' '
	git reset --hard &&
	mkdir -p $rr &&
	cp .git/post-saved $rr/postimage &&
	test_must_fail git merge first &&
	test -f $rr/preimage
'

test_expect_success 'set up for garbage collection tests' '
	mkdir -p $rr &&
	echo Hello >$rr/preimage &&
	echo World >$rr/postimage &&

	sha2=4000000000000000000000000000000000000000 &&
	rr2=.git/rr-cache/$sha2 &&
	mkdir $rr2 &&
	echo Hello >$rr2/preimage &&

	almost_15_days_ago=$((60-15*86400)) &&
	just_over_15_days_ago=$((-1-15*86400)) &&
	almost_60_days_ago=$((60-60*86400)) &&
	just_over_60_days_ago=$((-1-60*86400)) &&

	test-tool chmtime =$just_over_60_days_ago $rr/preimage &&
	test-tool chmtime =$almost_60_days_ago $rr/postimage &&
	test-tool chmtime =$almost_15_days_ago $rr2/preimage
'

test_expect_success 'gc preserves young or recently used records' '
	git rerere gc &&
	test -f $rr/preimage &&
	test -f $rr2/preimage
'

test_expect_success 'old records rest in peace' '
	test-tool chmtime =$just_over_60_days_ago $rr/postimage &&
	test-tool chmtime =$just_over_15_days_ago $rr2/preimage &&
	git rerere gc &&
	! test -f $rr/preimage &&
	! test -f $rr2/preimage
'

rerere_gc_custom_expiry_test () {
	five_days="$1" right_now="$2"
	test_expect_success "rerere gc with custom expiry ($five_days, $right_now)" '
		rm -fr .git/rr-cache &&
		rr=.git/rr-cache/$ZERO_OID &&
		mkdir -p "$rr" &&
		>"$rr/preimage" &&
		>"$rr/postimage" &&

		two_days_ago=$((-2*86400)) &&
		test-tool chmtime =$two_days_ago "$rr/preimage" &&
		test-tool chmtime =$two_days_ago "$rr/postimage" &&

		find .git/rr-cache -type f | sort >original &&

		git -c "gc.rerereresolved=$five_days" \
		    -c "gc.rerereunresolved=$five_days" rerere gc &&
		find .git/rr-cache -type f | sort >actual &&
		test_cmp original actual &&

		git -c "gc.rerereresolved=$five_days" \
		    -c "gc.rerereunresolved=$right_now" rerere gc &&
		find .git/rr-cache -type f | sort >actual &&
		test_cmp original actual &&

		git -c "gc.rerereresolved=$right_now" \
		    -c "gc.rerereunresolved=$right_now" rerere gc &&
		find .git/rr-cache -type f | sort >actual &&
		test_must_be_empty actual
	'
}

rerere_gc_custom_expiry_test 5 0

rerere_gc_custom_expiry_test 5.days.ago now

test_expect_success 'setup: file2 added differently in two branches' '
	git reset --hard &&

	git checkout -b fourth &&
	echo Hallo >file2 &&
	git add file2 &&
	test_tick &&
	git commit -m version1 &&

	git checkout third &&
	echo Bello >file2 &&
	git add file2 &&
	test_tick &&
	git commit -m version2 &&

	test_must_fail git merge fourth &&
	echo Cello >file2 &&
	git add file2 &&
	git commit -m resolution
'

test_expect_success 'resolution was recorded properly' '
	echo Cello >expected &&

	git reset --hard HEAD~2 &&
	git checkout -b fifth &&

	echo Hallo >file3 &&
	git add file3 &&
	test_tick &&
	git commit -m version1 &&

	git checkout third &&
	echo Bello >file3 &&
	git add file3 &&
	test_tick &&
	git commit -m version2 &&
	git tag version2 &&

	test_must_fail git merge fifth &&
	test_cmp expected file3 &&
	test_must_fail git update-index --refresh
'

test_expect_success 'rerere.autoupdate' '
	git config rerere.autoupdate true &&
	git reset --hard &&
	git checkout version2 &&
	test_must_fail git merge fifth &&
	git update-index --refresh
'

test_expect_success 'merge --rerere-autoupdate' '
	test_might_fail git config --unset rerere.autoupdate &&
	git reset --hard &&
	git checkout version2 &&
	test_must_fail git merge --rerere-autoupdate fifth &&
	git update-index --refresh
'

test_expect_success 'merge --no-rerere-autoupdate' '
	headblob=$(git rev-parse version2:file3) &&
	mergeblob=$(git rev-parse fifth:file3) &&
	cat >expected <<-EOF &&
	100644 $headblob 2	file3
	100644 $mergeblob 3	file3
	EOF

	git config rerere.autoupdate true &&
	git reset --hard &&
	git checkout version2 &&
	test_must_fail git merge --no-rerere-autoupdate fifth &&
	git ls-files -u >actual &&
	test_cmp expected actual
'

test_expect_success 'set up an unresolved merge' '
	headblob=$(git rev-parse version2:file3) &&
	mergeblob=$(git rev-parse fifth:file3) &&
	cat >expected.unresolved <<-EOF &&
	100644 $headblob 2	file3
	100644 $mergeblob 3	file3
	EOF

	test_might_fail git config --unset rerere.autoupdate &&
	git reset --hard &&
	git checkout version2 &&
	fifth=$(git rev-parse fifth) &&
	echo "$fifth		branch 'fifth' of ." |
	git fmt-merge-msg >msg &&
	ancestor=$(git merge-base version2 fifth) &&
	test_must_fail git merge-recursive "$ancestor" -- HEAD fifth &&

	git ls-files --stage >failedmerge &&
	cp file3 file3.conflict &&

	git ls-files -u >actual &&
	test_cmp expected.unresolved actual
'

test_expect_success 'explicit rerere' '
	test_might_fail git config --unset rerere.autoupdate &&
	git rm -fr --cached . &&
	git update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	test_must_fail git update-index --refresh -q &&

	git rerere &&
	git ls-files -u >actual &&
	test_cmp expected.unresolved actual
'

test_expect_success 'explicit rerere with autoupdate' '
	git config rerere.autoupdate true &&
	git rm -fr --cached . &&
	git update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	test_must_fail git update-index --refresh -q &&

	git rerere &&
	git update-index --refresh
'

test_expect_success 'explicit rerere --rerere-autoupdate overrides' '
	git config rerere.autoupdate false &&
	git rm -fr --cached . &&
	git update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	git rerere &&
	git ls-files -u >actual1 &&

	git rm -fr --cached . &&
	git update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	git rerere --rerere-autoupdate &&
	git update-index --refresh &&

	git rm -fr --cached . &&
	git update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	git rerere --rerere-autoupdate --no-rerere-autoupdate &&
	git ls-files -u >actual2 &&

	git rm -fr --cached . &&
	git update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	git rerere --rerere-autoupdate --no-rerere-autoupdate --rerere-autoupdate &&
	git update-index --refresh &&

	test_cmp expected.unresolved actual1 &&
	test_cmp expected.unresolved actual2
'

test_expect_success 'rerere --no-no-rerere-autoupdate' '
	git rm -fr --cached . &&
	git update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	test_must_fail git rerere --no-no-rerere-autoupdate 2>err &&
	test_i18ngrep [Uu]sage err &&
	test_must_fail git update-index --refresh
'

test_expect_success 'rerere -h' '
	test_must_fail git rerere -h >help &&
	test_i18ngrep [Uu]sage help
'

concat_insert () {
	last=$1
	shift
	cat early && printf "%s\n" "$@" && cat late "$last"
}

count_pre_post () {
	find .git/rr-cache/ -type f -name "preimage*" >actual &&
	test_line_count = "$1" actual &&
	find .git/rr-cache/ -type f -name "postimage*" >actual &&
	test_line_count = "$2" actual
}

merge_conflict_resolve () {
	git reset --hard &&
	test_must_fail git merge six.1 &&
	# Resolution is to replace 7 with 6.1 and 6.2 (i.e. take both)
	concat_insert short 6.1 6.2 >file1 &&
	concat_insert long 6.1 6.2 >file2
}

test_expect_success 'multiple identical conflicts' '
	rm -fr .git/rr-cache &&
	mkdir .git/rr-cache &&
	git reset --hard &&

	test_seq 1 6 >early &&
	>late &&
	test_seq 11 15 >short &&
	test_seq 111 120 >long &&
	concat_insert short >file1 &&
	concat_insert long >file2 &&
	git add file1 file2 &&
	git commit -m base &&
	git tag base &&
	git checkout -b six.1 &&
	concat_insert short 6.1 >file1 &&
	concat_insert long 6.1 >file2 &&
	git add file1 file2 &&
	git commit -m 6.1 &&
	git checkout -b six.2 HEAD^ &&
	concat_insert short 6.2 >file1 &&
	concat_insert long 6.2 >file2 &&
	git add file1 file2 &&
	git commit -m 6.2 &&

	# At this point, six.1 and six.2
	# - derive from common ancestor that has two files
	#   1...6 7 11..15 (file1) and 1...6 7 111..120 (file2)
	# - six.1 replaces these 7s with 6.1
	# - six.2 replaces these 7s with 6.2

	merge_conflict_resolve &&

	# Check that rerere knows that file1 and file2 have conflicts

	printf "%s\n" file1 file2 >expect &&
	git ls-files -u | sed -e "s/^.*	//" | sort -u >actual &&
	test_cmp expect actual &&

	git rerere status | sort >actual &&
	test_cmp expect actual &&

	git rerere remaining >actual &&
	test_cmp expect actual &&

	count_pre_post 2 0 &&

	# Pretend that the conflicts were made quite some time ago
	test-tool chmtime -172800 $(find .git/rr-cache/ -type f) &&

	# Unresolved entries have not expired yet
	git -c gc.rerereresolved=5 -c gc.rerereunresolved=5 rerere gc &&
	count_pre_post 2 0 &&

	# Unresolved entries have expired
	git -c gc.rerereresolved=5 -c gc.rerereunresolved=1 rerere gc &&
	count_pre_post 0 0 &&

	# Recreate the conflicted state
	merge_conflict_resolve &&
	count_pre_post 2 0 &&

	# Clear it
	git rerere clear &&
	count_pre_post 0 0 &&

	# Recreate the conflicted state
	merge_conflict_resolve &&
	count_pre_post 2 0 &&

	# We resolved file1 and file2
	git rerere &&
	git rerere remaining >actual &&
	test_must_be_empty actual &&

	# We must have recorded both of them
	count_pre_post 2 2 &&

	# Now we should be able to resolve them both
	git reset --hard &&
	test_must_fail git merge six.1 &&
	git rerere &&

	git rerere remaining >actual &&
	test_must_be_empty actual &&

	concat_insert short 6.1 6.2 >file1.expect &&
	concat_insert long 6.1 6.2 >file2.expect &&
	test_cmp file1.expect file1 &&
	test_cmp file2.expect file2 &&

	# Forget resolution for file2
	git rerere forget file2 &&
	echo file2 >expect &&
	git rerere status >actual &&
	test_cmp expect actual &&
	count_pre_post 2 1 &&

	# file2 already has correct resolution, so record it again
	git rerere &&

	# Pretend that the resolutions are old again
	test-tool chmtime -172800 $(find .git/rr-cache/ -type f) &&

	# Resolved entries have not expired yet
	git -c gc.rerereresolved=5 -c gc.rerereunresolved=5 rerere gc &&

	count_pre_post 2 2 &&

	# Resolved entries have expired
	git -c gc.rerereresolved=1 -c gc.rerereunresolved=5 rerere gc &&
	count_pre_post 0 0
'

test_expect_success 'rerere with unexpected conflict markers does not crash' '
	git reset --hard &&

	git checkout -b branch-1 master &&
	echo "bar" >test &&
	git add test &&
	git commit -q -m two &&

	git reset --hard &&
	git checkout -b branch-2 master &&
	echo "foo" >test &&
	git add test &&
	git commit -q -a -m one &&

	test_must_fail git merge branch-1 &&
	echo "<<<<<<< a" >test &&
	git rerere &&

	git rerere clear
'

test_expect_success 'rerere with inner conflict markers' '
	git reset --hard &&

	git checkout -b A master &&
	echo "bar" >test &&
	git add test &&
	git commit -q -m two &&
	echo "baz" >test &&
	git add test &&
	git commit -q -m three &&

	git reset --hard &&
	git checkout -b B master &&
	echo "foo" >test &&
	git add test &&
	git commit -q -a -m one &&

	test_must_fail git merge A~ &&
	git add test &&
	git commit -q -m "will solve conflicts later" &&
	test_must_fail git merge A &&

	echo "resolved" >test &&
	git add test &&
	git commit -q -m "solved conflict" &&

	echo "resolved" >expect &&

	git reset --hard HEAD~~ &&
	test_must_fail git merge A~ &&
	git add test &&
	git commit -q -m "will solve conflicts later" &&
	test_must_fail git merge A &&
	cat test >actual &&
	test_cmp expect actual &&

	git add test &&
	git commit -m "rerere solved conflict" &&
	git reset --hard HEAD~ &&
	test_must_fail git merge A &&
	cat test >actual &&
	test_cmp expect actual
'

test_expect_success 'setup simple stage 1 handling' '
	test_create_repo stage_1_handling &&
	(
		cd stage_1_handling &&

		test_seq 1 10 >original &&
		git add original &&
		git commit -m original &&

		git checkout -b A master &&
		git mv original A &&
		git commit -m "rename to A" &&

		git checkout -b B master &&
		git mv original B &&
		git commit -m "rename to B"
	)
'

test_expect_success 'test simple stage 1 handling' '
	(
		cd stage_1_handling &&

		git config rerere.enabled true &&
		git checkout A^0 &&
		test_must_fail git merge B^0
	)
'

test_done
