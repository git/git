#!/bin/sh

test_description='range-diff tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Note that because of the range-diff's heuristics, test_commit does more
# harm than good.  We need some real history.

test_expect_success 'setup' '
	git fast-import <"$TEST_DIRECTORY"/t3206/history.export &&
	test_oid_cache <<-\EOF
	# topic
	t1 sha1:4de457d
	t2 sha1:fccce22
	t3 sha1:147e64e
	t4 sha1:a63e992
	t1 sha256:b89f8b9
	t2 sha256:5f12aad
	t3 sha256:ea8b273
	t4 sha256:14b7336

	# unmodified
	u1 sha1:35b9b25
	u2 sha1:de345ab
	u3 sha1:9af6654
	u4 sha1:2901f77
	u1 sha256:e3731be
	u2 sha256:14fadf8
	u3 sha256:736c4bc
	u4 sha256:673e77d

	# reordered
	r1 sha1:aca177a
	r2 sha1:14ad629
	r3 sha1:ee58208
	r4 sha1:307b27a
	r1 sha256:f59d3aa
	r2 sha256:fb261a8
	r3 sha256:cb2649b
	r4 sha256:958577e

	# removed (deleted)
	d1 sha1:7657159
	d2 sha1:43d84d3
	d3 sha1:a740396
	d1 sha256:e312513
	d2 sha256:eb19258
	d3 sha256:1ccb3c1

	# added
	a1 sha1:2716022
	a2 sha1:b62accd
	a3 sha1:df46cfa
	a4 sha1:3e64548
	a5 sha1:12b4063
	a1 sha256:d724f4d
	a2 sha256:1de7762
	a3 sha256:e159431
	a4 sha256:b3e483c
	a5 sha256:90866a7

	# rebased
	b1 sha1:cc9c443
	b2 sha1:c5d9641
	b3 sha1:28cc2b6
	b4 sha1:5628ab7
	b5 sha1:a31b12e
	b1 sha256:a1a8717
	b2 sha256:20a5862
	b3 sha256:587172a
	b4 sha256:2721c5d
	b5 sha256:7b57864

	# changed
	c1 sha1:a4b3333
	c2 sha1:f51d370
	c3 sha1:0559556
	c4 sha1:d966c5c
	c1 sha256:f8c2b9d
	c2 sha256:3fb6318
	c3 sha256:168ab68
	c4 sha256:3526539

	# changed-message
	m1 sha1:f686024
	m2 sha1:4ab067d
	m3 sha1:b9cb956
	m4 sha1:8add5f1
	m1 sha256:31e6281
	m2 sha256:a06bf1b
	m3 sha256:82dc654
	m4 sha256:48470c5

	# renamed
	n1 sha1:f258d75
	n2 sha1:017b62d
	n3 sha1:3ce7af6
	n4 sha1:1e6226b
	n1 sha256:ad52114
	n2 sha256:3b54c8f
	n3 sha256:3b0a644
	n4 sha256:e461653

	# mode change
	o1 sha1:4d39cb3
	o2 sha1:26c107f
	o3 sha1:4c1e0f5
	o1 sha256:d0dd598
	o2 sha256:c4a279e
	o3 sha256:78459d7

	# added and removed
	s1 sha1:096b1ba
	s2 sha1:d92e698
	s3 sha1:9a1db4d
	s4 sha1:fea3b5c
	s1 sha256:a7f9134
	s2 sha256:b4c2580
	s3 sha256:1d62aa2
	s4 sha256:48160e8

	# Empty delimiter (included so lines match neatly)
	__ sha1:-------
	__ sha256:-------
	EOF
'

test_expect_success 'simple A..B A..C (unmodified)' '
	git range-diff --no-color main..topic main..unmodified \
		>actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid u1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid u2) s/4/A/
	3:  $(test_oid t3) = 3:  $(test_oid u3) s/11/B/
	4:  $(test_oid t4) = 4:  $(test_oid u4) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'simple B...C (unmodified)' '
	git range-diff --no-color topic...unmodified >actual &&
	# same "expect" as above
	test_cmp expect actual
'

test_expect_success 'simple A B C (unmodified)' '
	git range-diff --no-color main topic unmodified >actual &&
	# same "expect" as above
	test_cmp expect actual
'

test_expect_success 'A^! and A^-<n> (unmodified)' '
	git range-diff --no-color topic^! unmodified^-1 >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t4) = 1:  $(test_oid u4) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'A^{/..} is not mistaken for a range' '
	test_must_fail git range-diff topic^.. topic^{/..} 2>error &&
	test_i18ngrep "not a commit range" error
'

test_expect_success 'trivial reordering' '
	git range-diff --no-color main topic reordered >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid r1) s/5/A/
	3:  $(test_oid t3) = 2:  $(test_oid r2) s/11/B/
	4:  $(test_oid t4) = 3:  $(test_oid r3) s/12/B/
	2:  $(test_oid t2) = 4:  $(test_oid r4) s/4/A/
	EOF
	test_cmp expect actual
'

test_expect_success 'removed a commit' '
	git range-diff --no-color main topic removed >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid d1) s/5/A/
	2:  $(test_oid t2) < -:  $(test_oid __) s/4/A/
	3:  $(test_oid t3) = 2:  $(test_oid d2) s/11/B/
	4:  $(test_oid t4) = 3:  $(test_oid d3) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'added a commit' '
	git range-diff --no-color main topic added >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid a1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid a2) s/4/A/
	-:  $(test_oid __) > 3:  $(test_oid a3) s/6/A/
	3:  $(test_oid t3) = 4:  $(test_oid a4) s/11/B/
	4:  $(test_oid t4) = 5:  $(test_oid a5) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'new base, A B C' '
	git range-diff --no-color main topic rebased >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid b1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid b2) s/4/A/
	3:  $(test_oid t3) = 3:  $(test_oid b3) s/11/B/
	4:  $(test_oid t4) = 4:  $(test_oid b4) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'new base, B...C' '
	# this syntax includes the commits from main!
	git range-diff --no-color topic...rebased >actual &&
	cat >expect <<-EOF &&
	-:  $(test_oid __) > 1:  $(test_oid b5) unrelated
	1:  $(test_oid t1) = 2:  $(test_oid b1) s/5/A/
	2:  $(test_oid t2) = 3:  $(test_oid b2) s/4/A/
	3:  $(test_oid t3) = 4:  $(test_oid b3) s/11/B/
	4:  $(test_oid t4) = 5:  $(test_oid b4) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'changed commit' '
	git range-diff --no-color topic...changed >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid c1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid c2) s/4/A/
	3:  $(test_oid t3) ! 3:  $(test_oid c3) s/11/B/
	    @@ file: A
	      9
	      10
	     -11
	    -+B
	    ++BB
	      12
	      13
	      14
	4:  $(test_oid t4) ! 4:  $(test_oid c4) s/12/B/
	    @@ file
	     @@ file: A
	      9
	      10
	    - B
	    + BB
	     -12
	     +B
	      13
	EOF
	test_cmp expect actual
'

test_expect_success 'changed commit with --no-patch diff option' '
	git range-diff --no-color --no-patch topic...changed >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid c1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid c2) s/4/A/
	3:  $(test_oid t3) ! 3:  $(test_oid c3) s/11/B/
	4:  $(test_oid t4) ! 4:  $(test_oid c4) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'changed commit with --stat diff option' '
	git range-diff --no-color --stat topic...changed >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid c1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid c2) s/4/A/
	3:  $(test_oid t3) ! 3:  $(test_oid c3) s/11/B/
	     a => b | 2 +-
	     1 file changed, 1 insertion(+), 1 deletion(-)
	4:  $(test_oid t4) ! 4:  $(test_oid c4) s/12/B/
	     a => b | 2 +-
	     1 file changed, 1 insertion(+), 1 deletion(-)
	EOF
	test_cmp expect actual
'

test_expect_success 'changed commit with sm config' '
	git range-diff --no-color --submodule=log topic...changed >actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid c1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid c2) s/4/A/
	3:  $(test_oid t3) ! 3:  $(test_oid c3) s/11/B/
	    @@ file: A
	      9
	      10
	     -11
	    -+B
	    ++BB
	      12
	      13
	      14
	4:  $(test_oid t4) ! 4:  $(test_oid c4) s/12/B/
	    @@ file
	     @@ file: A
	      9
	      10
	    - B
	    + BB
	     -12
	     +B
	      13
	EOF
	test_cmp expect actual
'

test_expect_success 'renamed file' '
	git range-diff --no-color --submodule=log topic...renamed-file >actual &&
	sed s/Z/\ /g >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid n1) s/5/A/
	2:  $(test_oid t2) ! 2:  $(test_oid n2) s/4/A/
	    @@ Metadata
	    ZAuthor: Thomas Rast <trast@inf.ethz.ch>
	    Z
	    Z ## Commit message ##
	    -    s/4/A/
	    +    s/4/A/ + rename file
	    Z
	    - ## file ##
	    + ## file => renamed-file ##
	    Z@@
	    Z 1
	    Z 2
	3:  $(test_oid t3) ! 3:  $(test_oid n3) s/11/B/
	    @@ Metadata
	    Z ## Commit message ##
	    Z    s/11/B/
	    Z
	    - ## file ##
	    -@@ file: A
	    + ## renamed-file ##
	    +@@ renamed-file: A
	    Z 8
	    Z 9
	    Z 10
	4:  $(test_oid t4) ! 4:  $(test_oid n4) s/12/B/
	    @@ Metadata
	    Z ## Commit message ##
	    Z    s/12/B/
	    Z
	    - ## file ##
	    -@@ file: A
	    + ## renamed-file ##
	    +@@ renamed-file: A
	    Z 9
	    Z 10
	    Z B
	EOF
	test_cmp expect actual
'

test_expect_success 'file with mode only change' '
	git range-diff --no-color --submodule=log topic...mode-only-change >actual &&
	sed s/Z/\ /g >expect <<-EOF &&
	1:  $(test_oid t2) ! 1:  $(test_oid o1) s/4/A/
	    @@ Metadata
	    ZAuthor: Thomas Rast <trast@inf.ethz.ch>
	    Z
	    Z ## Commit message ##
	    -    s/4/A/
	    +    s/4/A/ + add other-file
	    Z
	    Z ## file ##
	    Z@@
	    @@ file
	    Z A
	    Z 6
	    Z 7
	    +
	    + ## other-file (new) ##
	2:  $(test_oid t3) ! 2:  $(test_oid o2) s/11/B/
	    @@ Metadata
	    ZAuthor: Thomas Rast <trast@inf.ethz.ch>
	    Z
	    Z ## Commit message ##
	    -    s/11/B/
	    +    s/11/B/ + mode change other-file
	    Z
	    Z ## file ##
	    Z@@ file: A
	    @@ file: A
	    Z 12
	    Z 13
	    Z 14
	    +
	    + ## other-file (mode change 100644 => 100755) ##
	3:  $(test_oid t4) = 3:  $(test_oid o3) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'file added and later removed' '
	git range-diff --no-color --submodule=log topic...added-removed >actual &&
	sed s/Z/\ /g >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid s1) s/5/A/
	2:  $(test_oid t2) ! 2:  $(test_oid s2) s/4/A/
	    @@ Metadata
	    ZAuthor: Thomas Rast <trast@inf.ethz.ch>
	    Z
	    Z ## Commit message ##
	    -    s/4/A/
	    +    s/4/A/ + new-file
	    Z
	    Z ## file ##
	    Z@@
	    @@ file
	    Z A
	    Z 6
	    Z 7
	    +
	    + ## new-file (new) ##
	3:  $(test_oid t3) ! 3:  $(test_oid s3) s/11/B/
	    @@ Metadata
	    ZAuthor: Thomas Rast <trast@inf.ethz.ch>
	    Z
	    Z ## Commit message ##
	    -    s/11/B/
	    +    s/11/B/ + remove file
	    Z
	    Z ## file ##
	    Z@@ file: A
	    @@ file: A
	    Z 12
	    Z 13
	    Z 14
	    +
	    + ## new-file (deleted) ##
	4:  $(test_oid t4) = 4:  $(test_oid s4) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'no commits on one side' '
	git commit --amend -m "new message" &&
	git range-diff main HEAD@{1} HEAD
'

test_expect_success 'changed message' '
	git range-diff --no-color topic...changed-message >actual &&
	sed s/Z/\ /g >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid m1) s/5/A/
	2:  $(test_oid t2) ! 2:  $(test_oid m2) s/4/A/
	    @@ Metadata
	    Z ## Commit message ##
	    Z    s/4/A/
	    Z
	    +    Also a silly comment here!
	    +
	    Z ## file ##
	    Z@@
	    Z 1
	3:  $(test_oid t3) = 3:  $(test_oid m3) s/11/B/
	4:  $(test_oid t4) = 4:  $(test_oid m4) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'dual-coloring' '
	sed -e "s|^:||" >expect <<-EOF &&
	:<YELLOW>1:  $(test_oid c1) = 1:  $(test_oid m1) s/5/A/<RESET>
	:<RED>2:  $(test_oid c2) <RESET><YELLOW>!<RESET><GREEN> 2:  $(test_oid m2)<RESET><YELLOW> s/4/A/<RESET>
	:    <REVERSE><CYAN>@@<RESET> <RESET>Metadata<RESET>
	:      ## Commit message ##<RESET>
	:         s/4/A/<RESET>
	:     <RESET>
	:    <REVERSE><GREEN>+<RESET><BOLD>    Also a silly comment here!<RESET>
	:    <REVERSE><GREEN>+<RESET>
	:      ## file ##<RESET>
	:    <CYAN> @@<RESET>
	:      1<RESET>
	:<RED>3:  $(test_oid c3) <RESET><YELLOW>!<RESET><GREEN> 3:  $(test_oid m3)<RESET><YELLOW> s/11/B/<RESET>
	:    <REVERSE><CYAN>@@<RESET> <RESET>file: A<RESET>
	:      9<RESET>
	:      10<RESET>
	:    <RED> -11<RESET>
	:    <REVERSE><RED>-<RESET><FAINT;GREEN>+BB<RESET>
	:    <REVERSE><GREEN>+<RESET><BOLD;GREEN>+B<RESET>
	:      12<RESET>
	:      13<RESET>
	:      14<RESET>
	:<RED>4:  $(test_oid c4) <RESET><YELLOW>!<RESET><GREEN> 4:  $(test_oid m4)<RESET><YELLOW> s/12/B/<RESET>
	:    <REVERSE><CYAN>@@<RESET> <RESET>file<RESET>
	:    <CYAN> @@ file: A<RESET>
	:      9<RESET>
	:      10<RESET>
	:    <REVERSE><RED>-<RESET><FAINT> BB<RESET>
	:    <REVERSE><GREEN>+<RESET><BOLD> B<RESET>
	:    <RED> -12<RESET>
	:    <GREEN> +B<RESET>
	:      13<RESET>
	EOF
	git range-diff changed...changed-message --color --dual-color >actual.raw &&
	test_decode_color >actual <actual.raw &&
	test_cmp expect actual
'

for prev in topic main..topic
do
	test_expect_success "format-patch --range-diff=$prev" '
		git format-patch --cover-letter --range-diff=$prev \
			main..unmodified >actual &&
		test_when_finished "rm 000?-*" &&
		test_line_count = 5 actual &&
		test_i18ngrep "^Range-diff:$" 0000-* &&
		grep "= 1: .* s/5/A" 0000-* &&
		grep "= 2: .* s/4/A" 0000-* &&
		grep "= 3: .* s/11/B" 0000-* &&
		grep "= 4: .* s/12/B" 0000-*
	'
done

test_expect_success 'format-patch --range-diff as commentary' '
	git format-patch --range-diff=HEAD~1 HEAD~1 >actual &&
	test_when_finished "rm 0001-*" &&
	test_line_count = 1 actual &&
	test_i18ngrep "^Range-diff:$" 0001-* &&
	grep "> 1: .* new message" 0001-*
'

test_expect_success 'format-patch --range-diff reroll-count with a non-integer' '
	git format-patch --range-diff=HEAD~1 -v2.9 HEAD~1 >actual &&
	test_when_finished "rm v2.9-0001-*" &&
	test_line_count = 1 actual &&
	test_i18ngrep "^Range-diff:$" v2.9-0001-* &&
	grep "> 1: .* new message" v2.9-0001-*
'

test_expect_success 'format-patch --range-diff reroll-count with a integer' '
	git format-patch --range-diff=HEAD~1 -v2 HEAD~1 >actual &&
	test_when_finished "rm v2-0001-*" &&
	test_line_count = 1 actual &&
	test_i18ngrep "^Range-diff ..* v1:$" v2-0001-* &&
	grep "> 1: .* new message" v2-0001-*
'

test_expect_success 'format-patch --range-diff with v0' '
	git format-patch --range-diff=HEAD~1 -v0 HEAD~1 >actual &&
	test_when_finished "rm v0-0001-*" &&
	test_line_count = 1 actual &&
	test_i18ngrep "^Range-diff:$" v0-0001-* &&
	grep "> 1: .* new message" v0-0001-*
'

test_expect_success 'range-diff overrides diff.noprefix internally' '
	git -c diff.noprefix=true range-diff HEAD^...
'

test_expect_success 'basic with modified format.pretty with suffix' '
	git -c format.pretty="format:commit %H%d%n" range-diff \
		main..topic main..unmodified
'

test_expect_success 'basic with modified format.pretty without "commit "' '
	git -c format.pretty="format:%H%n" range-diff \
		main..topic main..unmodified
'

test_expect_success 'range-diff compares notes by default' '
	git notes add -m "topic note" topic &&
	git notes add -m "unmodified note" unmodified &&
	test_when_finished git notes remove topic unmodified &&
	git range-diff --no-color main..topic main..unmodified \
		>actual &&
	sed s/Z/\ /g >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid u1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid u2) s/4/A/
	3:  $(test_oid t3) = 3:  $(test_oid u3) s/11/B/
	4:  $(test_oid t4) ! 4:  $(test_oid u4) s/12/B/
	    @@ Commit message
	    Z
	    Z
	    Z ## Notes ##
	    -    topic note
	    +    unmodified note
	    Z
	    Z ## file ##
	    Z@@ file: A
	EOF
	test_cmp expect actual
'

test_expect_success 'range-diff with --no-notes' '
	git notes add -m "topic note" topic &&
	git notes add -m "unmodified note" unmodified &&
	test_when_finished git notes remove topic unmodified &&
	git range-diff --no-color --no-notes main..topic main..unmodified \
		>actual &&
	cat >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid u1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid u2) s/4/A/
	3:  $(test_oid t3) = 3:  $(test_oid u3) s/11/B/
	4:  $(test_oid t4) = 4:  $(test_oid u4) s/12/B/
	EOF
	test_cmp expect actual
'

test_expect_success 'range-diff with multiple --notes' '
	git notes --ref=note1 add -m "topic note1" topic &&
	git notes --ref=note1 add -m "unmodified note1" unmodified &&
	test_when_finished git notes --ref=note1 remove topic unmodified &&
	git notes --ref=note2 add -m "topic note2" topic &&
	git notes --ref=note2 add -m "unmodified note2" unmodified &&
	test_when_finished git notes --ref=note2 remove topic unmodified &&
	git range-diff --no-color --notes=note1 --notes=note2 main..topic main..unmodified \
		>actual &&
	sed s/Z/\ /g >expect <<-EOF &&
	1:  $(test_oid t1) = 1:  $(test_oid u1) s/5/A/
	2:  $(test_oid t2) = 2:  $(test_oid u2) s/4/A/
	3:  $(test_oid t3) = 3:  $(test_oid u3) s/11/B/
	4:  $(test_oid t4) ! 4:  $(test_oid u4) s/12/B/
	    @@ Commit message
	    Z
	    Z
	    Z ## Notes (note1) ##
	    -    topic note1
	    +    unmodified note1
	    Z
	    Z
	    Z ## Notes (note2) ##
	    -    topic note2
	    +    unmodified note2
	    Z
	    Z ## file ##
	    Z@@ file: A
	EOF
	test_cmp expect actual
'

test_expect_success 'format-patch --range-diff does not compare notes by default' '
	git notes add -m "topic note" topic &&
	git notes add -m "unmodified note" unmodified &&
	test_when_finished git notes remove topic unmodified &&
	git format-patch --cover-letter --range-diff=$prev \
		main..unmodified >actual &&
	test_when_finished "rm 000?-*" &&
	test_line_count = 5 actual &&
	test_i18ngrep "^Range-diff:$" 0000-* &&
	grep "= 1: .* s/5/A" 0000-* &&
	grep "= 2: .* s/4/A" 0000-* &&
	grep "= 3: .* s/11/B" 0000-* &&
	grep "= 4: .* s/12/B" 0000-* &&
	! grep "Notes" 0000-* &&
	! grep "note" 0000-*
'

test_expect_success 'format-patch --range-diff with --no-notes' '
	git notes add -m "topic note" topic &&
	git notes add -m "unmodified note" unmodified &&
	test_when_finished git notes remove topic unmodified &&
	git format-patch --no-notes --cover-letter --range-diff=$prev \
		main..unmodified >actual &&
	test_when_finished "rm 000?-*" &&
	test_line_count = 5 actual &&
	test_i18ngrep "^Range-diff:$" 0000-* &&
	grep "= 1: .* s/5/A" 0000-* &&
	grep "= 2: .* s/4/A" 0000-* &&
	grep "= 3: .* s/11/B" 0000-* &&
	grep "= 4: .* s/12/B" 0000-* &&
	! grep "Notes" 0000-* &&
	! grep "note" 0000-*
'

test_expect_success 'format-patch --range-diff with --notes' '
	git notes add -m "topic note" topic &&
	git notes add -m "unmodified note" unmodified &&
	test_when_finished git notes remove topic unmodified &&
	git format-patch --notes --cover-letter --range-diff=$prev \
		main..unmodified >actual &&
	test_when_finished "rm 000?-*" &&
	test_line_count = 5 actual &&
	test_i18ngrep "^Range-diff:$" 0000-* &&
	grep "= 1: .* s/5/A" 0000-* &&
	grep "= 2: .* s/4/A" 0000-* &&
	grep "= 3: .* s/11/B" 0000-* &&
	grep "! 4: .* s/12/B" 0000-* &&
	sed s/Z/\ /g >expect <<-EOF &&
	    @@ Commit message
	    Z
	    Z
	    Z ## Notes ##
	    -    topic note
	    +    unmodified note
	    Z
	    Z ## file ##
	    Z@@ file: A
	EOF
	sed "/@@ Commit message/,/@@ file: A/!d" 0000-* >actual &&
	test_cmp expect actual
'

test_expect_success 'format-patch --range-diff with format.notes config' '
	git notes add -m "topic note" topic &&
	git notes add -m "unmodified note" unmodified &&
	test_when_finished git notes remove topic unmodified &&
	test_config format.notes true &&
	git format-patch --cover-letter --range-diff=$prev \
		main..unmodified >actual &&
	test_when_finished "rm 000?-*" &&
	test_line_count = 5 actual &&
	test_i18ngrep "^Range-diff:$" 0000-* &&
	grep "= 1: .* s/5/A" 0000-* &&
	grep "= 2: .* s/4/A" 0000-* &&
	grep "= 3: .* s/11/B" 0000-* &&
	grep "! 4: .* s/12/B" 0000-* &&
	sed s/Z/\ /g >expect <<-EOF &&
	    @@ Commit message
	    Z
	    Z
	    Z ## Notes ##
	    -    topic note
	    +    unmodified note
	    Z
	    Z ## file ##
	    Z@@ file: A
	EOF
	sed "/@@ Commit message/,/@@ file: A/!d" 0000-* >actual &&
	test_cmp expect actual
'

test_expect_success 'format-patch --range-diff with multiple notes' '
	git notes --ref=note1 add -m "topic note1" topic &&
	git notes --ref=note1 add -m "unmodified note1" unmodified &&
	test_when_finished git notes --ref=note1 remove topic unmodified &&
	git notes --ref=note2 add -m "topic note2" topic &&
	git notes --ref=note2 add -m "unmodified note2" unmodified &&
	test_when_finished git notes --ref=note2 remove topic unmodified &&
	git format-patch --notes=note1 --notes=note2 --cover-letter --range-diff=$prev \
		main..unmodified >actual &&
	test_when_finished "rm 000?-*" &&
	test_line_count = 5 actual &&
	test_i18ngrep "^Range-diff:$" 0000-* &&
	grep "= 1: .* s/5/A" 0000-* &&
	grep "= 2: .* s/4/A" 0000-* &&
	grep "= 3: .* s/11/B" 0000-* &&
	grep "! 4: .* s/12/B" 0000-* &&
	sed s/Z/\ /g >expect <<-EOF &&
	    @@ Commit message
	    Z
	    Z
	    Z ## Notes (note1) ##
	    -    topic note1
	    +    unmodified note1
	    Z
	    Z
	    Z ## Notes (note2) ##
	    -    topic note2
	    +    unmodified note2
	    Z
	    Z ## file ##
	    Z@@ file: A
	EOF
	sed "/@@ Commit message/,/@@ file: A/!d" 0000-* >actual &&
	test_cmp expect actual
'

test_expect_success '--left-only/--right-only' '
	git switch --orphan left-right &&
	test_commit first &&
	test_commit unmatched &&
	test_commit common &&
	git switch -C left-right first &&
	git cherry-pick common &&

	git range-diff -s --left-only ...common >actual &&
	head_oid=$(git rev-parse --short HEAD) &&
	common_oid=$(git rev-parse --short common) &&
	echo "1:  $head_oid = 2:  $common_oid common" >expect &&
	test_cmp expect actual
'

test_done
