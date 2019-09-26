#!/bin/sh

test_description='range-diff tests'

. ./test-lib.sh

# Note that because of the range-diff's heuristics, test_commit does more
# harm than good.  We need some real history.

test_expect_success 'setup' '
	git fast-import < "$TEST_DIRECTORY"/t3206/history.export
'

test_expect_success 'simple A..B A..C (unmodified)' '
	git range-diff --no-color master..topic master..unmodified \
		>actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  35b9b25 s/5/A/
	2:  fccce22 = 2:  de345ab s/4/A/
	3:  147e64e = 3:  9af6654 s/11/B/
	4:  a63e992 = 4:  2901f77 s/12/B/
	EOF
	test_cmp expected actual
'

test_expect_success 'simple B...C (unmodified)' '
	git range-diff --no-color topic...unmodified >actual &&
	# same "expected" as above
	test_cmp expected actual
'

test_expect_success 'simple A B C (unmodified)' '
	git range-diff --no-color master topic unmodified >actual &&
	# same "expected" as above
	test_cmp expected actual
'

test_expect_success 'trivial reordering' '
	git range-diff --no-color master topic reordered >actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  aca177a s/5/A/
	3:  147e64e = 2:  14ad629 s/11/B/
	4:  a63e992 = 3:  ee58208 s/12/B/
	2:  fccce22 = 4:  307b27a s/4/A/
	EOF
	test_cmp expected actual
'

test_expect_success 'removed a commit' '
	git range-diff --no-color master topic removed >actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  7657159 s/5/A/
	2:  fccce22 < -:  ------- s/4/A/
	3:  147e64e = 2:  43d84d3 s/11/B/
	4:  a63e992 = 3:  a740396 s/12/B/
	EOF
	test_cmp expected actual
'

test_expect_success 'added a commit' '
	git range-diff --no-color master topic added >actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  2716022 s/5/A/
	2:  fccce22 = 2:  b62accd s/4/A/
	-:  ------- > 3:  df46cfa s/6/A/
	3:  147e64e = 4:  3e64548 s/11/B/
	4:  a63e992 = 5:  12b4063 s/12/B/
	EOF
	test_cmp expected actual
'

test_expect_success 'new base, A B C' '
	git range-diff --no-color master topic rebased >actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  cc9c443 s/5/A/
	2:  fccce22 = 2:  c5d9641 s/4/A/
	3:  147e64e = 3:  28cc2b6 s/11/B/
	4:  a63e992 = 4:  5628ab7 s/12/B/
	EOF
	test_cmp expected actual
'

test_expect_success 'new base, B...C' '
	# this syntax includes the commits from master!
	git range-diff --no-color topic...rebased >actual &&
	cat >expected <<-EOF &&
	-:  ------- > 1:  a31b12e unrelated
	1:  4de457d = 2:  cc9c443 s/5/A/
	2:  fccce22 = 3:  c5d9641 s/4/A/
	3:  147e64e = 4:  28cc2b6 s/11/B/
	4:  a63e992 = 5:  5628ab7 s/12/B/
	EOF
	test_cmp expected actual
'

test_expect_success 'changed commit' '
	git range-diff --no-color topic...changed >actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  a4b3333 s/5/A/
	2:  fccce22 = 2:  f51d370 s/4/A/
	3:  147e64e ! 3:  0559556 s/11/B/
	    @@ file: A
	      9
	      10
	     -11
	    -+B
	    ++BB
	      12
	      13
	      14
	4:  a63e992 ! 4:  d966c5c s/12/B/
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
	test_cmp expected actual
'

test_expect_success 'changed commit with --no-patch diff option' '
	git range-diff --no-color --no-patch topic...changed >actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  a4b3333 s/5/A/
	2:  fccce22 = 2:  f51d370 s/4/A/
	3:  147e64e ! 3:  0559556 s/11/B/
	4:  a63e992 ! 4:  d966c5c s/12/B/
	EOF
	test_cmp expected actual
'

test_expect_success 'changed commit with --stat diff option' '
	git range-diff --no-color --stat topic...changed >actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  a4b3333 s/5/A/
	     a => b | 0
	     1 file changed, 0 insertions(+), 0 deletions(-)
	2:  fccce22 = 2:  f51d370 s/4/A/
	     a => b | 0
	     1 file changed, 0 insertions(+), 0 deletions(-)
	3:  147e64e ! 3:  0559556 s/11/B/
	     a => b | 0
	     1 file changed, 0 insertions(+), 0 deletions(-)
	4:  a63e992 ! 4:  d966c5c s/12/B/
	     a => b | 0
	     1 file changed, 0 insertions(+), 0 deletions(-)
	EOF
	test_cmp expected actual
'

test_expect_success 'changed commit with sm config' '
	git range-diff --no-color --submodule=log topic...changed >actual &&
	cat >expected <<-EOF &&
	1:  4de457d = 1:  a4b3333 s/5/A/
	2:  fccce22 = 2:  f51d370 s/4/A/
	3:  147e64e ! 3:  0559556 s/11/B/
	    @@ file: A
	      9
	      10
	     -11
	    -+B
	    ++BB
	      12
	      13
	      14
	4:  a63e992 ! 4:  d966c5c s/12/B/
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
	test_cmp expected actual
'

test_expect_success 'renamed file' '
	git range-diff --no-color --submodule=log topic...renamed-file >actual &&
	sed s/Z/\ /g >expected <<-EOF &&
	1:  4de457d = 1:  f258d75 s/5/A/
	2:  fccce22 ! 2:  017b62d s/4/A/
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
	3:  147e64e ! 3:  3ce7af6 s/11/B/
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
	4:  a63e992 ! 4:  1e6226b s/12/B/
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
	test_cmp expected actual
'

test_expect_success 'file added and later removed' '
	git range-diff --no-color --submodule=log topic...added-removed >actual &&
	sed s/Z/\ /g >expected <<-EOF &&
	1:  4de457d = 1:  096b1ba s/5/A/
	2:  fccce22 ! 2:  d92e698 s/4/A/
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
	3:  147e64e ! 3:  9a1db4d s/11/B/
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
	4:  a63e992 = 4:  fea3b5c s/12/B/
	EOF
	test_cmp expected actual
'

test_expect_success 'no commits on one side' '
	git commit --amend -m "new message" &&
	git range-diff master HEAD@{1} HEAD
'

test_expect_success 'changed message' '
	git range-diff --no-color topic...changed-message >actual &&
	sed s/Z/\ /g >expected <<-EOF &&
	1:  4de457d = 1:  f686024 s/5/A/
	2:  fccce22 ! 2:  4ab067d s/4/A/
	    @@ Metadata
	    Z ## Commit message ##
	    Z    s/4/A/
	    Z
	    +    Also a silly comment here!
	    +
	    Z ## file ##
	    Z@@
	    Z 1
	3:  147e64e = 3:  b9cb956 s/11/B/
	4:  a63e992 = 4:  8add5f1 s/12/B/
	EOF
	test_cmp expected actual
'

test_expect_success 'dual-coloring' '
	sed -e "s|^:||" >expect <<-\EOF &&
	:<YELLOW>1:  a4b3333 = 1:  f686024 s/5/A/<RESET>
	:<RED>2:  f51d370 <RESET><YELLOW>!<RESET><GREEN> 2:  4ab067d<RESET><YELLOW> s/4/A/<RESET>
	:    <REVERSE><CYAN>@@<RESET> <RESET>Metadata<RESET>
	:      ## Commit message ##<RESET>
	:         s/4/A/<RESET>
	:     <RESET>
	:    <REVERSE><GREEN>+<RESET><BOLD>    Also a silly comment here!<RESET>
	:    <REVERSE><GREEN>+<RESET>
	:      ## file ##<RESET>
	:    <CYAN> @@<RESET>
	:      1<RESET>
	:<RED>3:  0559556 <RESET><YELLOW>!<RESET><GREEN> 3:  b9cb956<RESET><YELLOW> s/11/B/<RESET>
	:    <REVERSE><CYAN>@@<RESET> <RESET>file: A<RESET>
	:      9<RESET>
	:      10<RESET>
	:    <RED> -11<RESET>
	:    <REVERSE><RED>-<RESET><FAINT;GREEN>+BB<RESET>
	:    <REVERSE><GREEN>+<RESET><BOLD;GREEN>+B<RESET>
	:      12<RESET>
	:      13<RESET>
	:      14<RESET>
	:<RED>4:  d966c5c <RESET><YELLOW>!<RESET><GREEN> 4:  8add5f1<RESET><YELLOW> s/12/B/<RESET>
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

for prev in topic master..topic
do
	test_expect_success "format-patch --range-diff=$prev" '
		git format-patch --cover-letter --range-diff=$prev \
			master..unmodified >actual &&
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

test_done
