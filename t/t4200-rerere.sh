#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin
#

test_description='but rerere

! [fifth] version1
 ! [first] first
  ! [fourth] version1
   ! [main] initial
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
++++++ [main] initial
'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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

	but add a1 &&
	test_tick &&
	but cummit -q -a -m initial &&

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

	but checkout -b first &&
	test_tick &&
	but cummit -q -a -m first &&

	but checkout -b second main &&
	but show first:a1 |
	sed -e "s/To die, t/To die! T/" -e "s/Some title/Some Title/" >a1 &&
	echo "* END *" >>a1 &&
	test_tick &&
	but cummit -q -a -m second
'

test_expect_success 'nothing recorded without rerere' '
	rm -rf .but/rr-cache &&
	but config rerere.enabled false &&
	test_must_fail but merge first &&
	! test -d .but/rr-cache
'

test_expect_success 'activate rerere, old style (conflicting merge)' '
	but reset --hard &&
	mkdir .but/rr-cache &&
	test_might_fail but config --unset rerere.enabled &&
	test_must_fail but merge first &&

	sha1=$(perl -pe "s/	.*//" .but/MERGE_RR) &&
	rr=.but/rr-cache/$sha1 &&
	grep "^=======\$" $rr/preimage &&
	! test -f $rr/postimage &&
	! test -f $rr/thisimage
'

test_expect_success 'rerere.enabled works, too' '
	rm -rf .but/rr-cache &&
	but config rerere.enabled true &&
	but reset --hard &&
	test_must_fail but merge first &&

	sha1=$(perl -pe "s/	.*//" .but/MERGE_RR) &&
	rr=.but/rr-cache/$sha1 &&
	grep ^=======$ $rr/preimage
'

test_expect_success 'set up rr-cache' '
	rm -rf .but/rr-cache &&
	but config rerere.enabled true &&
	but reset --hard &&
	test_must_fail but merge first &&
	sha1=$(perl -pe "s/	.*//" .but/MERGE_RR) &&
	rr=.but/rr-cache/$sha1
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
	but show first:a1 >a1 &&
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
	but rerere diff >out &&
	test_cmp expect out
'

test_expect_success 'rerere status' '
	echo a1 >expect &&
	but rerere status >out &&
	test_cmp expect out
'

test_expect_success 'first postimage wins' '
	but show first:a1 | sed "s/To die: t/To die! T/" >expect &&

	but cummit -q -a -m "prefer first over second" &&
	test -f $rr/postimage &&

	oldmtimepost=$(test-tool chmtime --get -60 $rr/postimage) &&

	but checkout -b third main &&
	but show second^:a1 | sed "s/To die: t/To die! T/" >a1 &&
	but cummit -q -a -m third &&

	test_must_fail but merge first &&
	# rerere kicked in
	! grep "^=======\$" a1 &&
	test_cmp expect a1
'

test_expect_success 'rerere updates postimage timestamp' '
	newmtimepost=$(test-tool chmtime --get $rr/postimage) &&
	test $oldmtimepost -lt $newmtimepost
'

test_expect_success 'rerere clear' '
	mv $rr/postimage .but/post-saved &&
	echo "$sha1	a1" | perl -pe "y/\012/\000/" >.but/MERGE_RR &&
	but rerere clear &&
	! test -d $rr
'

test_expect_success 'leftover directory' '
	but reset --hard &&
	mkdir -p $rr &&
	test_must_fail but merge first &&
	test -f $rr/preimage
'

test_expect_success 'missing preimage' '
	but reset --hard &&
	mkdir -p $rr &&
	cp .but/post-saved $rr/postimage &&
	test_must_fail but merge first &&
	test -f $rr/preimage
'

test_expect_success 'set up for garbage collection tests' '
	mkdir -p $rr &&
	echo Hello >$rr/preimage &&
	echo World >$rr/postimage &&

	sha2=$(test_oid deadbeef) &&
	rr2=.but/rr-cache/$sha2 &&
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
	but rerere gc &&
	test -f $rr/preimage &&
	test -f $rr2/preimage
'

test_expect_success 'old records rest in peace' '
	test-tool chmtime =$just_over_60_days_ago $rr/postimage &&
	test-tool chmtime =$just_over_15_days_ago $rr2/preimage &&
	but rerere gc &&
	! test -f $rr/preimage &&
	! test -f $rr2/preimage
'

rerere_gc_custom_expiry_test () {
	five_days="$1" right_now="$2"
	test_expect_success "rerere gc with custom expiry ($five_days, $right_now)" '
		rm -fr .but/rr-cache &&
		rr=.but/rr-cache/$ZERO_OID &&
		mkdir -p "$rr" &&
		>"$rr/preimage" &&
		>"$rr/postimage" &&

		two_days_ago=$((-2*86400)) &&
		test-tool chmtime =$two_days_ago "$rr/preimage" &&
		test-tool chmtime =$two_days_ago "$rr/postimage" &&

		find .but/rr-cache -type f | sort >original &&

		but -c "gc.rerereresolved=$five_days" \
		    -c "gc.rerereunresolved=$five_days" rerere gc &&
		find .but/rr-cache -type f | sort >actual &&
		test_cmp original actual &&

		but -c "gc.rerereresolved=$five_days" \
		    -c "gc.rerereunresolved=$right_now" rerere gc &&
		find .but/rr-cache -type f | sort >actual &&
		test_cmp original actual &&

		but -c "gc.rerereresolved=$right_now" \
		    -c "gc.rerereunresolved=$right_now" rerere gc &&
		find .but/rr-cache -type f | sort >actual &&
		test_must_be_empty actual
	'
}

rerere_gc_custom_expiry_test 5 0

rerere_gc_custom_expiry_test 5.days.ago now

test_expect_success 'setup: file2 added differently in two branches' '
	but reset --hard &&

	but checkout -b fourth &&
	echo Hallo >file2 &&
	but add file2 &&
	test_tick &&
	but cummit -m version1 &&

	but checkout third &&
	echo Bello >file2 &&
	but add file2 &&
	test_tick &&
	but cummit -m version2 &&

	test_must_fail but merge fourth &&
	echo Cello >file2 &&
	but add file2 &&
	but cummit -m resolution
'

test_expect_success 'resolution was recorded properly' '
	echo Cello >expected &&

	but reset --hard HEAD~2 &&
	but checkout -b fifth &&

	echo Hallo >file3 &&
	but add file3 &&
	test_tick &&
	but cummit -m version1 &&

	but checkout third &&
	echo Bello >file3 &&
	but add file3 &&
	test_tick &&
	but cummit -m version2 &&
	but tag version2 &&

	test_must_fail but merge fifth &&
	test_cmp expected file3 &&
	test_must_fail but update-index --refresh
'

test_expect_success 'rerere.autoupdate' '
	but config rerere.autoupdate true &&
	but reset --hard &&
	but checkout version2 &&
	test_must_fail but merge fifth &&
	but update-index --refresh
'

test_expect_success 'merge --rerere-autoupdate' '
	test_might_fail but config --unset rerere.autoupdate &&
	but reset --hard &&
	but checkout version2 &&
	test_must_fail but merge --rerere-autoupdate fifth &&
	but update-index --refresh
'

test_expect_success 'merge --no-rerere-autoupdate' '
	headblob=$(but rev-parse version2:file3) &&
	mergeblob=$(but rev-parse fifth:file3) &&
	cat >expected <<-EOF &&
	100644 $headblob 2	file3
	100644 $mergeblob 3	file3
	EOF

	but config rerere.autoupdate true &&
	but reset --hard &&
	but checkout version2 &&
	test_must_fail but merge --no-rerere-autoupdate fifth &&
	but ls-files -u >actual &&
	test_cmp expected actual
'

test_expect_success 'set up an unresolved merge' '
	headblob=$(but rev-parse version2:file3) &&
	mergeblob=$(but rev-parse fifth:file3) &&
	cat >expected.unresolved <<-EOF &&
	100644 $headblob 2	file3
	100644 $mergeblob 3	file3
	EOF

	test_might_fail but config --unset rerere.autoupdate &&
	but reset --hard &&
	but checkout version2 &&
	fifth=$(but rev-parse fifth) &&
	echo "$fifth		branch fifth of ." |
	but fmt-merge-msg >msg &&
	ancestor=$(but merge-base version2 fifth) &&
	test_must_fail but merge-recursive "$ancestor" -- HEAD fifth &&

	but ls-files --stage >failedmerge &&
	cp file3 file3.conflict &&

	but ls-files -u >actual &&
	test_cmp expected.unresolved actual
'

test_expect_success 'explicit rerere' '
	test_might_fail but config --unset rerere.autoupdate &&
	but rm -fr --cached . &&
	but update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	test_must_fail but update-index --refresh -q &&

	but rerere &&
	but ls-files -u >actual &&
	test_cmp expected.unresolved actual
'

test_expect_success 'explicit rerere with autoupdate' '
	but config rerere.autoupdate true &&
	but rm -fr --cached . &&
	but update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	test_must_fail but update-index --refresh -q &&

	but rerere &&
	but update-index --refresh
'

test_expect_success 'explicit rerere --rerere-autoupdate overrides' '
	but config rerere.autoupdate false &&
	but rm -fr --cached . &&
	but update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	but rerere &&
	but ls-files -u >actual1 &&

	but rm -fr --cached . &&
	but update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	but rerere --rerere-autoupdate &&
	but update-index --refresh &&

	but rm -fr --cached . &&
	but update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	but rerere --rerere-autoupdate --no-rerere-autoupdate &&
	but ls-files -u >actual2 &&

	but rm -fr --cached . &&
	but update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	but rerere --rerere-autoupdate --no-rerere-autoupdate --rerere-autoupdate &&
	but update-index --refresh &&

	test_cmp expected.unresolved actual1 &&
	test_cmp expected.unresolved actual2
'

test_expect_success 'rerere --no-no-rerere-autoupdate' '
	but rm -fr --cached . &&
	but update-index --index-info <failedmerge &&
	cp file3.conflict file3 &&
	test_must_fail but rerere --no-no-rerere-autoupdate 2>err &&
	test_i18ngrep [Uu]sage err &&
	test_must_fail but update-index --refresh
'

test_expect_success 'rerere -h' '
	test_must_fail but rerere -h >help &&
	test_i18ngrep [Uu]sage help
'

concat_insert () {
	last=$1
	shift
	cat early && printf "%s\n" "$@" && cat late "$last"
}

count_pre_post () {
	find .but/rr-cache/ -type f -name "preimage*" >actual &&
	test_line_count = "$1" actual &&
	find .but/rr-cache/ -type f -name "postimage*" >actual &&
	test_line_count = "$2" actual
}

merge_conflict_resolve () {
	but reset --hard &&
	test_must_fail but merge six.1 &&
	# Resolution is to replace 7 with 6.1 and 6.2 (i.e. take both)
	concat_insert short 6.1 6.2 >file1 &&
	concat_insert long 6.1 6.2 >file2
}

test_expect_success 'multiple identical conflicts' '
	rm -fr .but/rr-cache &&
	mkdir .but/rr-cache &&
	but reset --hard &&

	test_seq 1 6 >early &&
	>late &&
	test_seq 11 15 >short &&
	test_seq 111 120 >long &&
	concat_insert short >file1 &&
	concat_insert long >file2 &&
	but add file1 file2 &&
	but cummit -m base &&
	but tag base &&
	but checkout -b six.1 &&
	concat_insert short 6.1 >file1 &&
	concat_insert long 6.1 >file2 &&
	but add file1 file2 &&
	but cummit -m 6.1 &&
	but checkout -b six.2 HEAD^ &&
	concat_insert short 6.2 >file1 &&
	concat_insert long 6.2 >file2 &&
	but add file1 file2 &&
	but cummit -m 6.2 &&

	# At this point, six.1 and six.2
	# - derive from common ancestor that has two files
	#   1...6 7 11..15 (file1) and 1...6 7 111..120 (file2)
	# - six.1 replaces these 7s with 6.1
	# - six.2 replaces these 7s with 6.2

	merge_conflict_resolve &&

	# Check that rerere knows that file1 and file2 have conflicts

	printf "%s\n" file1 file2 >expect &&
	but ls-files -u | sed -e "s/^.*	//" | sort -u >actual &&
	test_cmp expect actual &&

	but rerere status | sort >actual &&
	test_cmp expect actual &&

	but rerere remaining >actual &&
	test_cmp expect actual &&

	count_pre_post 2 0 &&

	# Pretend that the conflicts were made quite some time ago
	test-tool chmtime -172800 $(find .but/rr-cache/ -type f) &&

	# Unresolved entries have not expired yet
	but -c gc.rerereresolved=5 -c gc.rerereunresolved=5 rerere gc &&
	count_pre_post 2 0 &&

	# Unresolved entries have expired
	but -c gc.rerereresolved=5 -c gc.rerereunresolved=1 rerere gc &&
	count_pre_post 0 0 &&

	# Recreate the conflicted state
	merge_conflict_resolve &&
	count_pre_post 2 0 &&

	# Clear it
	but rerere clear &&
	count_pre_post 0 0 &&

	# Recreate the conflicted state
	merge_conflict_resolve &&
	count_pre_post 2 0 &&

	# We resolved file1 and file2
	but rerere &&
	but rerere remaining >actual &&
	test_must_be_empty actual &&

	# We must have recorded both of them
	count_pre_post 2 2 &&

	# Now we should be able to resolve them both
	but reset --hard &&
	test_must_fail but merge six.1 &&
	but rerere &&

	but rerere remaining >actual &&
	test_must_be_empty actual &&

	concat_insert short 6.1 6.2 >file1.expect &&
	concat_insert long 6.1 6.2 >file2.expect &&
	test_cmp file1.expect file1 &&
	test_cmp file2.expect file2 &&

	# Forget resolution for file2
	but rerere forget file2 &&
	echo file2 >expect &&
	but rerere status >actual &&
	test_cmp expect actual &&
	count_pre_post 2 1 &&

	# file2 already has correct resolution, so record it again
	but rerere &&

	# Pretend that the resolutions are old again
	test-tool chmtime -172800 $(find .but/rr-cache/ -type f) &&

	# Resolved entries have not expired yet
	but -c gc.rerereresolved=5 -c gc.rerereunresolved=5 rerere gc &&

	count_pre_post 2 2 &&

	# Resolved entries have expired
	but -c gc.rerereresolved=1 -c gc.rerereunresolved=5 rerere gc &&
	count_pre_post 0 0
'

test_expect_success 'rerere with unexpected conflict markers does not crash' '
	but reset --hard &&

	but checkout -b branch-1 main &&
	echo "bar" >test &&
	but add test &&
	but cummit -q -m two &&

	but reset --hard &&
	but checkout -b branch-2 main &&
	echo "foo" >test &&
	but add test &&
	but cummit -q -a -m one &&

	test_must_fail but merge branch-1 &&
	echo "<<<<<<< a" >test &&
	but rerere &&

	but rerere clear
'

test_expect_success 'rerere with inner conflict markers' '
	but reset --hard &&

	but checkout -b A main &&
	echo "bar" >test &&
	but add test &&
	but cummit -q -m two &&
	echo "baz" >test &&
	but add test &&
	but cummit -q -m three &&

	but reset --hard &&
	but checkout -b B main &&
	echo "foo" >test &&
	but add test &&
	but cummit -q -a -m one &&

	test_must_fail but merge A~ &&
	but add test &&
	but cummit -q -m "will solve conflicts later" &&
	test_must_fail but merge A &&

	echo "resolved" >test &&
	but add test &&
	but cummit -q -m "solved conflict" &&

	echo "resolved" >expect &&

	but reset --hard HEAD~~ &&
	test_must_fail but merge A~ &&
	but add test &&
	but cummit -q -m "will solve conflicts later" &&
	test_must_fail but merge A &&
	cat test >actual &&
	test_cmp expect actual &&

	but add test &&
	but cummit -m "rerere solved conflict" &&
	but reset --hard HEAD~ &&
	test_must_fail but merge A &&
	cat test >actual &&
	test_cmp expect actual
'

test_expect_success 'setup simple stage 1 handling' '
	test_create_repo stage_1_handling &&
	(
		cd stage_1_handling &&

		test_seq 1 10 >original &&
		but add original &&
		but cummit -m original &&

		but checkout -b A main &&
		but mv original A &&
		but cummit -m "rename to A" &&

		but checkout -b B main &&
		but mv original B &&
		but cummit -m "rename to B"
	)
'

test_expect_success 'test simple stage 1 handling' '
	(
		cd stage_1_handling &&

		but config rerere.enabled true &&
		but checkout A^0 &&
		test_must_fail but merge B^0
	)
'

test_done
