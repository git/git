#!/bin/sh

test_description='but log'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"
. "$TEST_DIRECTORY/lib-terminal.sh"
. "$TEST_DIRECTORY/lib-log-graph.sh"

test_cmp_graph () {
	lib_test_cmp_graph --format=%s "$@"
}

test_expect_success setup '

	echo one >one &&
	but add one &&
	test_tick &&
	but cummit -m initial &&

	echo ichi >one &&
	but add one &&
	test_tick &&
	but cummit -m second &&

	but mv one ichi &&
	test_tick &&
	but cummit -m third &&

	cp ichi ein &&
	but add ein &&
	test_tick &&
	but cummit -m fourth &&

	mkdir a &&
	echo ni >a/two &&
	but add a/two &&
	test_tick &&
	but cummit -m fifth  &&

	but rm a/two &&
	test_tick &&
	but cummit -m sixth

'

printf "sixth\nfifth\nfourth\nthird\nsecond\ninitial" > expect
test_expect_success 'pretty' '

	but log --pretty="format:%s" > actual &&
	test_cmp expect actual
'

printf "sixth\nfifth\nfourth\nthird\nsecond\ninitial\n" > expect
test_expect_success 'pretty (tformat)' '

	but log --pretty="tformat:%s" > actual &&
	test_cmp expect actual
'

test_expect_success 'pretty (shortcut)' '

	but log --pretty="%s" > actual &&
	test_cmp expect actual
'

test_expect_success 'format' '

	but log --format="%s" > actual &&
	test_cmp expect actual
'

cat > expect << EOF
 This is
  the sixth
  cummit.
 This is
  the fifth
  cummit.
EOF

test_expect_success 'format %w(11,1,2)' '

	but log -2 --format="%w(11,1,2)This is the %s cummit." > actual &&
	test_cmp expect actual
'

test_expect_success 'format %w(,1,2)' '

	but log -2 --format="%w(,1,2)This is%nthe %s%ncummit." > actual &&
	test_cmp expect actual
'

cat > expect << EOF
$(but rev-parse --short :/sixth  ) sixth
$(but rev-parse --short :/fifth  ) fifth
$(but rev-parse --short :/fourth ) fourth
$(but rev-parse --short :/third  ) third
$(but rev-parse --short :/second ) second
$(but rev-parse --short :/initial) initial
EOF
test_expect_success 'oneline' '

	but log --oneline > actual &&
	test_cmp expect actual
'

test_expect_success 'diff-filter=A' '

	but log --no-renames --pretty="format:%s" --diff-filter=A HEAD > actual &&
	but log --no-renames --pretty="format:%s" --diff-filter A HEAD > actual-separate &&
	printf "fifth\nfourth\nthird\ninitial" > expect &&
	test_cmp expect actual &&
	test_cmp expect actual-separate

'

test_expect_success 'diff-filter=M' '

	but log --pretty="format:%s" --diff-filter=M HEAD >actual &&
	printf "second" >expect &&
	test_cmp expect actual

'

test_expect_success 'diff-filter=D' '

	but log --no-renames --pretty="format:%s" --diff-filter=D HEAD >actual &&
	printf "sixth\nthird" >expect &&
	test_cmp expect actual

'

test_expect_success 'diff-filter=R' '

	but log -M --pretty="format:%s" --diff-filter=R HEAD >actual &&
	printf "third" >expect &&
	test_cmp expect actual

'

test_expect_success 'multiple --diff-filter bits' '

	but log -M --pretty="format:%s" --diff-filter=R HEAD >expect &&
	but log -M --pretty="format:%s" --diff-filter=Ra HEAD >actual &&
	test_cmp expect actual &&
	but log -M --pretty="format:%s" --diff-filter=aR HEAD >actual &&
	test_cmp expect actual &&
	but log -M --pretty="format:%s" \
		--diff-filter=a --diff-filter=R HEAD >actual &&
	test_cmp expect actual

'

test_expect_success 'diff-filter=C' '

	but log -C -C --pretty="format:%s" --diff-filter=C HEAD >actual &&
	printf "fourth" >expect &&
	test_cmp expect actual

'

test_expect_success 'but log --follow' '

	but log --follow --pretty="format:%s" ichi >actual &&
	printf "third\nsecond\ninitial" >expect &&
	test_cmp expect actual
'

test_expect_success 'but config log.follow works like --follow' '
	test_config log.follow true &&
	but log --pretty="format:%s" ichi >actual &&
	printf "third\nsecond\ninitial" >expect &&
	test_cmp expect actual
'

test_expect_success 'but config log.follow does not die with multiple paths' '
	test_config log.follow true &&
	but log --pretty="format:%s" ichi ein
'

test_expect_success 'but config log.follow does not die with no paths' '
	test_config log.follow true &&
	but log --
'

test_expect_success 'but config log.follow is overridden by --no-follow' '
	test_config log.follow true &&
	but log --no-follow --pretty="format:%s" ichi >actual &&
	printf "third" >expect &&
	test_cmp expect actual
'

# Note that these cummits are intentionally listed out of order.
last_three="$(but rev-parse :/fourth :/sixth :/fifth)"
cat > expect << EOF
$(but rev-parse --short :/sixth ) sixth
$(but rev-parse --short :/fifth ) fifth
$(but rev-parse --short :/fourth) fourth
EOF
test_expect_success 'but log --no-walk <cummits> sorts by cummit time' '
	but log --no-walk --oneline $last_three > actual &&
	test_cmp expect actual
'

test_expect_success 'but log --no-walk=sorted <cummits> sorts by cummit time' '
	but log --no-walk=sorted --oneline $last_three > actual &&
	test_cmp expect actual
'

cat > expect << EOF
=== $(but rev-parse --short :/sixth ) sixth
=== $(but rev-parse --short :/fifth ) fifth
=== $(but rev-parse --short :/fourth) fourth
EOF
test_expect_success 'but log --line-prefix="=== " --no-walk <cummits> sorts by cummit time' '
	but log --line-prefix="=== " --no-walk --oneline $last_three > actual &&
	test_cmp expect actual
'

cat > expect << EOF
$(but rev-parse --short :/fourth) fourth
$(but rev-parse --short :/sixth ) sixth
$(but rev-parse --short :/fifth ) fifth
EOF
test_expect_success 'but log --no-walk=unsorted <cummits> leaves list of cummits as given' '
	but log --no-walk=unsorted --oneline $last_three > actual &&
	test_cmp expect actual
'

test_expect_success 'but show <cummits> leaves list of cummits as given' '
	but show --oneline -s $last_three > actual &&
	test_cmp expect actual
'

test_expect_success 'setup case sensitivity tests' '
	echo case >one &&
	test_tick &&
	but add one &&
	but cummit -a -m Second
'

test_expect_success 'log --grep' '
	echo second >expect &&
	but log -1 --pretty="tformat:%s" --grep=sec >actual &&
	test_cmp expect actual
'

cat > expect << EOF
second
initial
EOF
test_expect_success 'log --invert-grep --grep' '
	# Fixed
	but -c grep.patternType=fixed log --pretty="tformat:%s" --invert-grep --grep=th --grep=Sec >actual &&
	test_cmp expect actual &&

	# POSIX basic
	but -c grep.patternType=basic log --pretty="tformat:%s" --invert-grep --grep=t[h] --grep=S[e]c >actual &&
	test_cmp expect actual &&

	# POSIX extended
	but -c grep.patternType=extended log --pretty="tformat:%s" --invert-grep --grep=t[h] --grep=S[e]c >actual &&
	test_cmp expect actual &&

	# PCRE
	if test_have_prereq PCRE
	then
		but -c grep.patternType=perl log --pretty="tformat:%s" --invert-grep --grep=t[h] --grep=S[e]c >actual &&
		test_cmp expect actual
	fi
'

test_expect_success 'log --invert-grep --grep -i' '
	echo initial >expect &&

	# Fixed
	but -c grep.patternType=fixed log --pretty="tformat:%s" --invert-grep -i --grep=th --grep=Sec >actual &&
	test_cmp expect actual &&

	# POSIX basic
	but -c grep.patternType=basic log --pretty="tformat:%s" --invert-grep -i --grep=t[h] --grep=S[e]c >actual &&
	test_cmp expect actual &&

	# POSIX extended
	but -c grep.patternType=extended log --pretty="tformat:%s" --invert-grep -i --grep=t[h] --grep=S[e]c >actual &&
	test_cmp expect actual &&

	# PCRE
	if test_have_prereq PCRE
	then
		but -c grep.patternType=perl log --pretty="tformat:%s" --invert-grep -i --grep=t[h] --grep=S[e]c >actual &&
		test_cmp expect actual
	fi
'

test_expect_success 'log --grep option parsing' '
	echo second >expect &&
	but log -1 --pretty="tformat:%s" --grep sec >actual &&
	test_cmp expect actual &&
	test_must_fail but log -1 --pretty="tformat:%s" --grep
'

test_expect_success 'log -i --grep' '
	echo Second >expect &&
	but log -1 --pretty="tformat:%s" -i --grep=sec >actual &&
	test_cmp expect actual
'

test_expect_success 'log --grep -i' '
	echo Second >expect &&

	# Fixed
	but log -1 --pretty="tformat:%s" --grep=sec -i >actual &&
	test_cmp expect actual &&

	# POSIX basic
	but -c grep.patternType=basic log -1 --pretty="tformat:%s" --grep=s[e]c -i >actual &&
	test_cmp expect actual &&

	# POSIX extended
	but -c grep.patternType=extended log -1 --pretty="tformat:%s" --grep=s[e]c -i >actual &&
	test_cmp expect actual &&

	# PCRE
	if test_have_prereq PCRE
	then
		but -c grep.patternType=perl log -1 --pretty="tformat:%s" --grep=s[e]c -i >actual &&
		test_cmp expect actual
	fi
'

test_expect_success 'log -F -E --grep=<ere> uses ere' '
	echo second >expect &&
	# basic would need \(s\) to do the same
	but log -1 --pretty="tformat:%s" -F -E --grep="(s).c.nd" >actual &&
	test_cmp expect actual
'

test_expect_success PCRE 'log -F -E --perl-regexp --grep=<pcre> uses PCRE' '
	test_when_finished "rm -rf num_cummits" &&
	but init num_cummits &&
	(
		cd num_cummits &&
		test_cummit 1d &&
		test_cummit 2e
	) &&

	# In PCRE \d in [\d] is like saying "0-9", and matches the 2
	# in 2e...
	echo 2e >expect &&
	but -C num_cummits log -1 --pretty="tformat:%s" -F -E --perl-regexp --grep="[\d]" >actual &&
	test_cmp expect actual &&

	# ...in POSIX basic and extended it is the same as [d],
	# i.e. "d", which matches 1d, but does not match 2e.
	echo 1d >expect &&
	but -C num_cummits log -1 --pretty="tformat:%s" -F -E --grep="[\d]" >actual &&
	test_cmp expect actual
'

test_expect_success 'log with grep.patternType configuration' '
	but -c grep.patterntype=fixed \
	log -1 --pretty=tformat:%s --grep=s.c.nd >actual &&
	test_must_be_empty actual
'

test_expect_success 'log with grep.patternType configuration and command line' '
	echo second >expect &&
	but -c grep.patterntype=fixed \
	log -1 --pretty=tformat:%s --basic-regexp --grep=s.c.nd >actual &&
	test_cmp expect actual
'

test_expect_success !FAIL_PREREQS 'log with various grep.patternType configurations & command-lines' '
	but init pattern-type &&
	(
		cd pattern-type &&
		test_cummit 1 file A &&

		# The tagname is overridden here because creating a
		# tag called "(1|2)" as test_cummit would otherwise
		# implicitly do would fail on e.g. MINGW.
		test_cummit "(1|2)" file B 2 &&

		echo "(1|2)" >expect.fixed &&
		cp expect.fixed expect.basic &&
		cp expect.fixed expect.extended &&
		cp expect.fixed expect.perl &&

		# A strcmp-like match with fixed.
		but -c grep.patternType=fixed log --pretty=tformat:%s \
			--grep="(1|2)" >actual.fixed &&

		# POSIX basic matches (, | and ) literally.
		but -c grep.patternType=basic log --pretty=tformat:%s \
			--grep="(.|.)" >actual.basic &&

		# POSIX extended needs to have | escaped to match it
		# literally, whereas under basic this is the same as
		# (|2), i.e. it would also match "1". This test checks
		# for extended by asserting that it is not matching
		# what basic would match.
		but -c grep.patternType=extended log --pretty=tformat:%s \
			--grep="\|2" >actual.extended &&
		if test_have_prereq PCRE
		then
			# Only PCRE would match [\d]\| with only
			# "(1|2)" due to [\d]. POSIX basic would match
			# both it and "1" since similarly to the
			# extended match above it is the same as
			# \([\d]\|\). POSIX extended would
			# match neither.
			but -c grep.patternType=perl log --pretty=tformat:%s \
				--grep="[\d]\|" >actual.perl &&
			test_cmp expect.perl actual.perl
		fi &&
		test_cmp expect.fixed actual.fixed &&
		test_cmp expect.basic actual.basic &&
		test_cmp expect.extended actual.extended &&

		but log --pretty=tformat:%s -F \
			--grep="(1|2)" >actual.fixed.short-arg &&
		but log --pretty=tformat:%s -E \
			--grep="\|2" >actual.extended.short-arg &&
		if test_have_prereq PCRE
		then
			but log --pretty=tformat:%s -P \
				--grep="[\d]\|" >actual.perl.short-arg
		else
			test_must_fail but log -P \
				--grep="[\d]\|"
		fi &&
		test_cmp expect.fixed actual.fixed.short-arg &&
		test_cmp expect.extended actual.extended.short-arg &&
		if test_have_prereq PCRE
		then
			test_cmp expect.perl actual.perl.short-arg
		fi &&

		but log --pretty=tformat:%s --fixed-strings \
			--grep="(1|2)" >actual.fixed.long-arg &&
		but log --pretty=tformat:%s --basic-regexp \
			--grep="(.|.)" >actual.basic.long-arg &&
		but log --pretty=tformat:%s --extended-regexp \
			--grep="\|2" >actual.extended.long-arg &&
		if test_have_prereq PCRE
		then
			but log --pretty=tformat:%s --perl-regexp \
				--grep="[\d]\|" >actual.perl.long-arg &&
			test_cmp expect.perl actual.perl.long-arg
		else
			test_must_fail but log --perl-regexp \
				--grep="[\d]\|"
		fi &&
		test_cmp expect.fixed actual.fixed.long-arg &&
		test_cmp expect.basic actual.basic.long-arg &&
		test_cmp expect.extended actual.extended.long-arg
	)
'

for cmd in show whatchanged reflog format-patch
do
	case "$cmd" in
	format-patch) myarg="HEAD~.." ;;
	*) myarg= ;;
	esac

	test_expect_success "$cmd: understands grep.patternType, like 'log'" '
		but init "pattern-type-$cmd" &&
		(
			cd "pattern-type-$cmd" &&
			test_cummit 1 file A &&
			test_cummit "(1|2)" file B 2 &&

			but -c grep.patternType=fixed $cmd --grep="..." $myarg >actual &&
			test_must_be_empty actual &&

			but -c grep.patternType=basic $cmd --grep="..." $myarg >actual &&
			test_file_not_empty actual
		)
	'
done

test_expect_success 'log --author' '
	cat >expect <<-\EOF &&
	Author: <BOLD;RED>A U<RESET> Thor <author@example.com>
	EOF
	but log -1 --color=always --author="A U" >log &&
	grep Author log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'log --cummitter' '
	cat >expect <<-\EOF &&
	cummit:     C O Mitter <cummitter@<BOLD;RED>example<RESET>.com>
	EOF
	but log -1 --color=always --pretty=fuller --cummitter="example" >log &&
	grep "cummit:" log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'log -i --grep with color' '
	cat >expect <<-\EOF &&
	    <BOLD;RED>Sec<RESET>ond
	    <BOLD;RED>sec<RESET>ond
	EOF
	but log --color=always -i --grep=^sec >log &&
	grep -i sec log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success '-c color.grep.selected log --grep' '
	cat >expect <<-\EOF &&
	    <GREEN>th<RESET><BOLD;RED>ir<RESET><GREEN>d<RESET>
	EOF
	but -c color.grep.selected="green" log --color=always --grep=ir >log &&
	grep ir log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success '-c color.grep.matchSelected log --grep' '
	cat >expect <<-\EOF &&
	    <BLUE>i<RESET>n<BLUE>i<RESET>t<BLUE>i<RESET>al
	EOF
	but -c color.grep.matchSelected="blue" log --color=always --grep=i >log &&
	grep al log >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

cat > expect <<EOF
* Second
* sixth
* fifth
* fourth
* third
* second
* initial
EOF

test_expect_success 'simple log --graph' '
	test_cmp_graph
'

cat > expect <<EOF
123 * Second
123 * sixth
123 * fifth
123 * fourth
123 * third
123 * second
123 * initial
EOF

test_expect_success 'simple log --graph --line-prefix="123 "' '
	test_cmp_graph --line-prefix="123 "
'

test_expect_success 'set up merge history' '
	but checkout -b side HEAD~4 &&
	test_cummit side-1 1 1 &&
	test_cummit side-2 2 2 &&
	but checkout main &&
	but merge side
'

cat > expect <<\EOF
*   Merge branch 'side'
|\
| * side-2
| * side-1
* | Second
* | sixth
* | fifth
* | fourth
|/
* third
* second
* initial
EOF

test_expect_success 'log --graph with merge' '
	test_cmp_graph --date-order
'

cat > expect <<\EOF
| | | *   Merge branch 'side'
| | | |\
| | | | * side-2
| | | | * side-1
| | | * | Second
| | | * | sixth
| | | * | fifth
| | | * | fourth
| | | |/
| | | * third
| | | * second
| | | * initial
EOF

test_expect_success 'log --graph --line-prefix="| | | " with merge' '
	test_cmp_graph --line-prefix="| | | " --date-order
'

cat > expect.colors <<\EOF
*   Merge branch 'side'
<BLUE>|<RESET><CYAN>\<RESET>
<BLUE>|<RESET> * side-2
<BLUE>|<RESET> * side-1
* <CYAN>|<RESET> Second
* <CYAN>|<RESET> sixth
* <CYAN>|<RESET> fifth
* <CYAN>|<RESET> fourth
<CYAN>|<RESET><CYAN>/<RESET>
* third
* second
* initial
EOF

test_expect_success 'log --graph with merge with log.graphColors' '
	test_config log.graphColors " blue,invalid-color, cyan, red  , " &&
	lib_test_cmp_colored_graph --date-order --format=%s
'

test_expect_success 'log --raw --graph -m with merge' '
	but log --raw --graph --oneline -m main | head -n 500 >actual &&
	grep "initial" actual
'

test_expect_success 'diff-tree --graph' '
	but diff-tree --graph main^ | head -n 500 >actual &&
	grep "one" actual
'

cat > expect <<\EOF
*   cummit main
|\  Merge: A B
| | Author: A U Thor <author@example.com>
| |
| |     Merge branch 'side'
| |
| * cummit tags/side-2
| | Author: A U Thor <author@example.com>
| |
| |     side-2
| |
| * cummit tags/side-1
| | Author: A U Thor <author@example.com>
| |
| |     side-1
| |
* | cummit main~1
| | Author: A U Thor <author@example.com>
| |
| |     Second
| |
* | cummit main~2
| | Author: A U Thor <author@example.com>
| |
| |     sixth
| |
* | cummit main~3
| | Author: A U Thor <author@example.com>
| |
| |     fifth
| |
* | cummit main~4
|/  Author: A U Thor <author@example.com>
|
|       fourth
|
* cummit tags/side-1~1
| Author: A U Thor <author@example.com>
|
|     third
|
* cummit tags/side-1~2
| Author: A U Thor <author@example.com>
|
|     second
|
* cummit tags/side-1~3
  Author: A U Thor <author@example.com>

      initial
EOF

test_expect_success 'log --graph with full output' '
	but log --graph --date-order --pretty=short |
		but name-rev --name-only --annotate-stdin |
		sed "s/Merge:.*/Merge: A B/;s/ *\$//" >actual &&
	test_cmp expect actual
'

test_expect_success 'set up more tangled history' '
	but checkout -b tangle HEAD~6 &&
	test_cummit tangle-a tangle-a a &&
	but merge main~3 &&
	but merge side~1 &&
	but checkout main &&
	but merge tangle &&
	but checkout -b reach &&
	test_cummit reach &&
	but checkout main &&
	but checkout -b octopus-a &&
	test_cummit octopus-a &&
	but checkout main &&
	but checkout -b octopus-b &&
	test_cummit octopus-b &&
	but checkout main &&
	test_cummit seventh &&
	but merge octopus-a octopus-b &&
	but merge reach
'

cat > expect <<\EOF
*   Merge tag 'reach'
|\
| \
|  \
*-. \   Merge tags 'octopus-a' and 'octopus-b'
|\ \ \
* | | | seventh
| | * | octopus-b
| |/ /
|/| |
| * | octopus-a
|/ /
| * reach
|/
*   Merge branch 'tangle'
|\
| *   Merge branch 'side' (early part) into tangle
| |\
| * \   Merge branch 'main' (early part) into tangle
| |\ \
| * | | tangle-a
* | | |   Merge branch 'side'
|\ \ \ \
| * | | | side-2
| | |_|/
| |/| |
| * | | side-1
* | | | Second
* | | | sixth
| |_|/
|/| |
* | | fifth
* | | fourth
|/ /
* / third
|/
* second
* initial
EOF

test_expect_success 'log --graph with merge' '
	test_cmp_graph --date-order
'

test_expect_success 'log.decorate configuration' '
	but log --oneline --no-decorate >expect.none &&
	but log --oneline --decorate >expect.short &&
	but log --oneline --decorate=full >expect.full &&

	echo "[log] decorate" >>.but/config &&
	but log --oneline >actual &&
	test_cmp expect.short actual &&

	test_config log.decorate true &&
	but log --oneline >actual &&
	test_cmp expect.short actual &&
	but log --oneline --decorate=full >actual &&
	test_cmp expect.full actual &&
	but log --oneline --decorate=no >actual &&
	test_cmp expect.none actual &&

	test_config log.decorate no &&
	but log --oneline >actual &&
	test_cmp expect.none actual &&
	but log --oneline --decorate >actual &&
	test_cmp expect.short actual &&
	but log --oneline --decorate=full >actual &&
	test_cmp expect.full actual &&

	test_config log.decorate 1 &&
	but log --oneline >actual &&
	test_cmp expect.short actual &&
	but log --oneline --decorate=full >actual &&
	test_cmp expect.full actual &&
	but log --oneline --decorate=no >actual &&
	test_cmp expect.none actual &&

	test_config log.decorate short &&
	but log --oneline >actual &&
	test_cmp expect.short actual &&
	but log --oneline --no-decorate >actual &&
	test_cmp expect.none actual &&
	but log --oneline --decorate=full >actual &&
	test_cmp expect.full actual &&

	test_config log.decorate full &&
	but log --oneline >actual &&
	test_cmp expect.full actual &&
	but log --oneline --no-decorate >actual &&
	test_cmp expect.none actual &&
	but log --oneline --decorate >actual &&
	test_cmp expect.short actual &&

	test_unconfig log.decorate &&
	but log --pretty=raw >expect.raw &&
	test_config log.decorate full &&
	but log --pretty=raw >actual &&
	test_cmp expect.raw actual

'

test_expect_success 'decorate-refs with glob' '
	cat >expect.decorate <<-\EOF &&
	Merge-tag-reach
	Merge-tags-octopus-a-and-octopus-b
	seventh
	octopus-b (octopus-b)
	octopus-a (octopus-a)
	reach
	EOF
	cat >expect.no-decorate <<-\EOF &&
	Merge-tag-reach
	Merge-tags-octopus-a-and-octopus-b
	seventh
	octopus-b
	octopus-a
	reach
	EOF
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs="heads/octopus*" >actual &&
	test_cmp expect.decorate actual &&
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs-exclude="heads/octopus*" \
		--decorate-refs="heads/octopus*" >actual &&
	test_cmp expect.no-decorate actual &&
	but -c log.excludeDecoration="heads/octopus*" log \
		-n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs="heads/octopus*" >actual &&
	test_cmp expect.decorate actual
'

test_expect_success 'decorate-refs without globs' '
	cat >expect.decorate <<-\EOF &&
	Merge-tag-reach
	Merge-tags-octopus-a-and-octopus-b
	seventh
	octopus-b
	octopus-a
	reach (tag: reach)
	EOF
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs="tags/reach" >actual &&
	test_cmp expect.decorate actual
'

test_expect_success 'multiple decorate-refs' '
	cat >expect.decorate <<-\EOF &&
	Merge-tag-reach
	Merge-tags-octopus-a-and-octopus-b
	seventh
	octopus-b (octopus-b)
	octopus-a (octopus-a)
	reach (tag: reach)
	EOF
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs="heads/octopus*" \
		--decorate-refs="tags/reach" >actual &&
    test_cmp expect.decorate actual
'

test_expect_success 'decorate-refs-exclude with glob' '
	cat >expect.decorate <<-\EOF &&
	Merge-tag-reach (HEAD -> main)
	Merge-tags-octopus-a-and-octopus-b
	seventh (tag: seventh)
	octopus-b (tag: octopus-b)
	octopus-a (tag: octopus-a)
	reach (tag: reach, reach)
	EOF
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs-exclude="heads/octopus*" >actual &&
	test_cmp expect.decorate actual &&
	but -c log.excludeDecoration="heads/octopus*" log \
		-n6 --decorate=short --pretty="tformat:%f%d" >actual &&
	test_cmp expect.decorate actual
'

test_expect_success 'decorate-refs-exclude without globs' '
	cat >expect.decorate <<-\EOF &&
	Merge-tag-reach (HEAD -> main)
	Merge-tags-octopus-a-and-octopus-b
	seventh (tag: seventh)
	octopus-b (tag: octopus-b, octopus-b)
	octopus-a (tag: octopus-a, octopus-a)
	reach (reach)
	EOF
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs-exclude="tags/reach" >actual &&
	test_cmp expect.decorate actual &&
	but -c log.excludeDecoration="tags/reach" log \
		-n6 --decorate=short --pretty="tformat:%f%d" >actual &&
	test_cmp expect.decorate actual
'

test_expect_success 'multiple decorate-refs-exclude' '
	cat >expect.decorate <<-\EOF &&
	Merge-tag-reach (HEAD -> main)
	Merge-tags-octopus-a-and-octopus-b
	seventh (tag: seventh)
	octopus-b (tag: octopus-b)
	octopus-a (tag: octopus-a)
	reach (reach)
	EOF
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs-exclude="heads/octopus*" \
		--decorate-refs-exclude="tags/reach" >actual &&
	test_cmp expect.decorate actual &&
	but -c log.excludeDecoration="heads/octopus*" \
		-c log.excludeDecoration="tags/reach" log \
		-n6 --decorate=short --pretty="tformat:%f%d" >actual &&
	test_cmp expect.decorate actual &&
	but -c log.excludeDecoration="heads/octopus*" log \
		--decorate-refs-exclude="tags/reach" \
		-n6 --decorate=short --pretty="tformat:%f%d" >actual &&
	test_cmp expect.decorate actual
'

test_expect_success 'decorate-refs and decorate-refs-exclude' '
	cat >expect.no-decorate <<-\EOF &&
	Merge-tag-reach (main)
	Merge-tags-octopus-a-and-octopus-b
	seventh
	octopus-b
	octopus-a
	reach (reach)
	EOF
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs="heads/*" \
		--decorate-refs-exclude="heads/oc*" >actual &&
	test_cmp expect.no-decorate actual
'

test_expect_success 'deocrate-refs and log.excludeDecoration' '
	cat >expect.decorate <<-\EOF &&
	Merge-tag-reach (main)
	Merge-tags-octopus-a-and-octopus-b
	seventh
	octopus-b (octopus-b)
	octopus-a (octopus-a)
	reach (reach)
	EOF
	but -c log.excludeDecoration="heads/oc*" log \
		--decorate-refs="heads/*" \
		-n6 --decorate=short --pretty="tformat:%f%d" >actual &&
	test_cmp expect.decorate actual
'

test_expect_success 'decorate-refs-exclude and simplify-by-decoration' '
	cat >expect.decorate <<-\EOF &&
	Merge-tag-reach (HEAD -> main)
	reach (tag: reach, reach)
	seventh (tag: seventh)
	Merge-branch-tangle
	Merge-branch-side-early-part-into-tangle (tangle)
	tangle-a (tag: tangle-a)
	EOF
	but log -n6 --decorate=short --pretty="tformat:%f%d" \
		--decorate-refs-exclude="*octopus*" \
		--simplify-by-decoration >actual &&
	test_cmp expect.decorate actual &&
	but -c log.excludeDecoration="*octopus*" log \
		-n6 --decorate=short --pretty="tformat:%f%d" \
		--simplify-by-decoration >actual &&
	test_cmp expect.decorate actual
'

test_expect_success 'decorate-refs with implied decorate from format' '
	cat >expect <<-\EOF &&
	side-2 (tag: side-2)
	side-1
	EOF
	but log --no-walk --format="%s%d" \
		--decorate-refs="*side-2" side-1 side-2 \
		>actual &&
	test_cmp expect actual
'

test_expect_success 'implied decorate does not override option' '
	cat >expect <<-\EOF &&
	side-2 (tag: refs/tags/side-2, refs/heads/side)
	side-1 (tag: refs/tags/side-1)
	EOF
	but log --no-walk --format="%s%d" \
		--decorate=full side-1 side-2 \
		>actual &&
	test_cmp expect actual
'

test_expect_success 'decorate-refs and simplify-by-decoration without output' '
	cat >expect <<-\EOF &&
	side-2
	initial
	EOF
	# Do not just use a --format without %d here; we want to
	# make sure that we did not accidentally turn on displaying
	# the decorations, too. And that requires one of the regular
	# formats.
	but log --decorate-refs="*side-2" --oneline \
		--simplify-by-decoration >actual.raw &&
	sed "s/^[0-9a-f]* //" <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'log.decorate config parsing' '
	but log --oneline --decorate=full >expect.full &&
	but log --oneline --decorate=short >expect.short &&

	test_config log.decorate full &&
	test_config log.mailmap true &&
	but log --oneline >actual &&
	test_cmp expect.full actual &&
	but log --oneline --decorate=short >actual &&
	test_cmp expect.short actual
'

test_expect_success TTY 'log output on a TTY' '
	but log --color --oneline --decorate >expect.short &&

	test_terminal but log --oneline >actual &&
	test_cmp expect.short actual
'

test_expect_success 'reflog is expected format' '
	but log -g --abbrev-cummit --pretty=oneline >expect &&
	but reflog >actual &&
	test_cmp expect actual
'

test_expect_success 'whatchanged is expected format' '
	but log --no-merges --raw >expect &&
	but whatchanged >actual &&
	test_cmp expect actual
'

test_expect_success 'log.abbrevcummit configuration' '
	but log --abbrev-cummit >expect.log.abbrev &&
	but log --no-abbrev-cummit >expect.log.full &&
	but log --pretty=raw >expect.log.raw &&
	but reflog --abbrev-cummit >expect.reflog.abbrev &&
	but reflog --no-abbrev-cummit >expect.reflog.full &&
	but whatchanged --abbrev-cummit >expect.whatchanged.abbrev &&
	but whatchanged --no-abbrev-cummit >expect.whatchanged.full &&

	test_config log.abbrevcummit true &&

	but log >actual &&
	test_cmp expect.log.abbrev actual &&
	but log --no-abbrev-cummit >actual &&
	test_cmp expect.log.full actual &&

	but log --pretty=raw >actual &&
	test_cmp expect.log.raw actual &&

	but reflog >actual &&
	test_cmp expect.reflog.abbrev actual &&
	but reflog --no-abbrev-cummit >actual &&
	test_cmp expect.reflog.full actual &&

	but whatchanged >actual &&
	test_cmp expect.whatchanged.abbrev actual &&
	but whatchanged --no-abbrev-cummit >actual &&
	test_cmp expect.whatchanged.full actual
'

test_expect_success 'show added path under "--follow -M"' '
	# This tests for a regression introduced in v1.7.2-rc0~103^2~2
	test_create_repo regression &&
	(
		cd regression &&
		test_cummit needs-another-cummit &&
		test_cummit foo.bar &&
		but log -M --follow -p foo.bar.t &&
		but log -M --follow --stat foo.bar.t &&
		but log -M --follow --name-only foo.bar.t
	)
'

test_expect_success 'but log -c --follow' '
	test_create_repo follow-c &&
	(
		cd follow-c &&
		test_cummit initial file original &&
		but rm file &&
		test_cummit rename file2 original &&
		but reset --hard initial &&
		test_cummit modify file foo &&
		but merge -m merge rename &&
		but log -c --follow file2
	)
'

cat >expect <<\EOF
*   cummit CUMMIT_OBJECT_NAME
|\  Merge: MERGE_PARENTS
| | Author: A U Thor <author@example.com>
| |
| |     Merge HEADS DESCRIPTION
| |
| * cummit CUMMIT_OBJECT_NAME
| | Author: A U Thor <author@example.com>
| |
| |     reach
| | ---
| |  reach.t | 1 +
| |  1 file changed, 1 insertion(+)
| |
| | diff --but a/reach.t b/reach.t
| | new file mode 100644
| | index BEFORE..AFTER
| | --- /dev/null
| | +++ b/reach.t
| | @@ -0,0 +1 @@
| | +reach
| |
|  \
*-. \   cummit CUMMIT_OBJECT_NAME
|\ \ \  Merge: MERGE_PARENTS
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     Merge HEADS DESCRIPTION
| | | |
| | * | cummit CUMMIT_OBJECT_NAME
| | |/  Author: A U Thor <author@example.com>
| | |
| | |       octopus-b
| | |   ---
| | |    octopus-b.t | 1 +
| | |    1 file changed, 1 insertion(+)
| | |
| | |   diff --but a/octopus-b.t b/octopus-b.t
| | |   new file mode 100644
| | |   index BEFORE..AFTER
| | |   --- /dev/null
| | |   +++ b/octopus-b.t
| | |   @@ -0,0 +1 @@
| | |   +octopus-b
| | |
| * | cummit CUMMIT_OBJECT_NAME
| |/  Author: A U Thor <author@example.com>
| |
| |       octopus-a
| |   ---
| |    octopus-a.t | 1 +
| |    1 file changed, 1 insertion(+)
| |
| |   diff --but a/octopus-a.t b/octopus-a.t
| |   new file mode 100644
| |   index BEFORE..AFTER
| |   --- /dev/null
| |   +++ b/octopus-a.t
| |   @@ -0,0 +1 @@
| |   +octopus-a
| |
* | cummit CUMMIT_OBJECT_NAME
|/  Author: A U Thor <author@example.com>
|
|       seventh
|   ---
|    seventh.t | 1 +
|    1 file changed, 1 insertion(+)
|
|   diff --but a/seventh.t b/seventh.t
|   new file mode 100644
|   index BEFORE..AFTER
|   --- /dev/null
|   +++ b/seventh.t
|   @@ -0,0 +1 @@
|   +seventh
|
*   cummit CUMMIT_OBJECT_NAME
|\  Merge: MERGE_PARENTS
| | Author: A U Thor <author@example.com>
| |
| |     Merge branch 'tangle'
| |
| *   cummit CUMMIT_OBJECT_NAME
| |\  Merge: MERGE_PARENTS
| | | Author: A U Thor <author@example.com>
| | |
| | |     Merge branch 'side' (early part) into tangle
| | |
| * |   cummit CUMMIT_OBJECT_NAME
| |\ \  Merge: MERGE_PARENTS
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     Merge branch 'main' (early part) into tangle
| | | |
| * | | cummit CUMMIT_OBJECT_NAME
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     tangle-a
| | | | ---
| | | |  tangle-a | 1 +
| | | |  1 file changed, 1 insertion(+)
| | | |
| | | | diff --but a/tangle-a b/tangle-a
| | | | new file mode 100644
| | | | index BEFORE..AFTER
| | | | --- /dev/null
| | | | +++ b/tangle-a
| | | | @@ -0,0 +1 @@
| | | | +a
| | | |
* | | |   cummit CUMMIT_OBJECT_NAME
|\ \ \ \  Merge: MERGE_PARENTS
| | | | | Author: A U Thor <author@example.com>
| | | | |
| | | | |     Merge branch 'side'
| | | | |
| * | | | cummit CUMMIT_OBJECT_NAME
| | |_|/  Author: A U Thor <author@example.com>
| |/| |
| | | |       side-2
| | | |   ---
| | | |    2 | 1 +
| | | |    1 file changed, 1 insertion(+)
| | | |
| | | |   diff --but a/2 b/2
| | | |   new file mode 100644
| | | |   index BEFORE..AFTER
| | | |   --- /dev/null
| | | |   +++ b/2
| | | |   @@ -0,0 +1 @@
| | | |   +2
| | | |
| * | | cummit CUMMIT_OBJECT_NAME
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     side-1
| | | | ---
| | | |  1 | 1 +
| | | |  1 file changed, 1 insertion(+)
| | | |
| | | | diff --but a/1 b/1
| | | | new file mode 100644
| | | | index BEFORE..AFTER
| | | | --- /dev/null
| | | | +++ b/1
| | | | @@ -0,0 +1 @@
| | | | +1
| | | |
* | | | cummit CUMMIT_OBJECT_NAME
| | | | Author: A U Thor <author@example.com>
| | | |
| | | |     Second
| | | | ---
| | | |  one | 1 +
| | | |  1 file changed, 1 insertion(+)
| | | |
| | | | diff --but a/one b/one
| | | | new file mode 100644
| | | | index BEFORE..AFTER
| | | | --- /dev/null
| | | | +++ b/one
| | | | @@ -0,0 +1 @@
| | | | +case
| | | |
* | | | cummit CUMMIT_OBJECT_NAME
| |_|/  Author: A U Thor <author@example.com>
|/| |
| | |       sixth
| | |   ---
| | |    a/two | 1 -
| | |    1 file changed, 1 deletion(-)
| | |
| | |   diff --but a/a/two b/a/two
| | |   deleted file mode 100644
| | |   index BEFORE..AFTER
| | |   --- a/a/two
| | |   +++ /dev/null
| | |   @@ -1 +0,0 @@
| | |   -ni
| | |
* | | cummit CUMMIT_OBJECT_NAME
| | | Author: A U Thor <author@example.com>
| | |
| | |     fifth
| | | ---
| | |  a/two | 1 +
| | |  1 file changed, 1 insertion(+)
| | |
| | | diff --but a/a/two b/a/two
| | | new file mode 100644
| | | index BEFORE..AFTER
| | | --- /dev/null
| | | +++ b/a/two
| | | @@ -0,0 +1 @@
| | | +ni
| | |
* | | cummit CUMMIT_OBJECT_NAME
|/ /  Author: A U Thor <author@example.com>
| |
| |       fourth
| |   ---
| |    ein | 1 +
| |    1 file changed, 1 insertion(+)
| |
| |   diff --but a/ein b/ein
| |   new file mode 100644
| |   index BEFORE..AFTER
| |   --- /dev/null
| |   +++ b/ein
| |   @@ -0,0 +1 @@
| |   +ichi
| |
* | cummit CUMMIT_OBJECT_NAME
|/  Author: A U Thor <author@example.com>
|
|       third
|   ---
|    ichi | 1 +
|    one  | 1 -
|    2 files changed, 1 insertion(+), 1 deletion(-)
|
|   diff --but a/ichi b/ichi
|   new file mode 100644
|   index BEFORE..AFTER
|   --- /dev/null
|   +++ b/ichi
|   @@ -0,0 +1 @@
|   +ichi
|   diff --but a/one b/one
|   deleted file mode 100644
|   index BEFORE..AFTER
|   --- a/one
|   +++ /dev/null
|   @@ -1 +0,0 @@
|   -ichi
|
* cummit CUMMIT_OBJECT_NAME
| Author: A U Thor <author@example.com>
|
|     second
| ---
|  one | 2 +-
|  1 file changed, 1 insertion(+), 1 deletion(-)
|
| diff --but a/one b/one
| index BEFORE..AFTER 100644
| --- a/one
| +++ b/one
| @@ -1 +1 @@
| -one
| +ichi
|
* cummit CUMMIT_OBJECT_NAME
  Author: A U Thor <author@example.com>

      initial
  ---
   one | 1 +
   1 file changed, 1 insertion(+)

  diff --but a/one b/one
  new file mode 100644
  index BEFORE..AFTER
  --- /dev/null
  +++ b/one
  @@ -0,0 +1 @@
  +one
EOF

test_expect_success 'log --graph with diff and stats' '
	lib_test_cmp_short_graph --no-renames --stat -p
'

cat >expect <<\EOF
*** *   cummit CUMMIT_OBJECT_NAME
*** |\  Merge: MERGE_PARENTS
*** | | Author: A U Thor <author@example.com>
*** | |
*** | |     Merge HEADS DESCRIPTION
*** | |
*** | * cummit CUMMIT_OBJECT_NAME
*** | | Author: A U Thor <author@example.com>
*** | |
*** | |     reach
*** | | ---
*** | |  reach.t | 1 +
*** | |  1 file changed, 1 insertion(+)
*** | |
*** | | diff --but a/reach.t b/reach.t
*** | | new file mode 100644
*** | | index BEFORE..AFTER
*** | | --- /dev/null
*** | | +++ b/reach.t
*** | | @@ -0,0 +1 @@
*** | | +reach
*** | |
*** |  \
*** *-. \   cummit CUMMIT_OBJECT_NAME
*** |\ \ \  Merge: MERGE_PARENTS
*** | | | | Author: A U Thor <author@example.com>
*** | | | |
*** | | | |     Merge HEADS DESCRIPTION
*** | | | |
*** | | * | cummit CUMMIT_OBJECT_NAME
*** | | |/  Author: A U Thor <author@example.com>
*** | | |
*** | | |       octopus-b
*** | | |   ---
*** | | |    octopus-b.t | 1 +
*** | | |    1 file changed, 1 insertion(+)
*** | | |
*** | | |   diff --but a/octopus-b.t b/octopus-b.t
*** | | |   new file mode 100644
*** | | |   index BEFORE..AFTER
*** | | |   --- /dev/null
*** | | |   +++ b/octopus-b.t
*** | | |   @@ -0,0 +1 @@
*** | | |   +octopus-b
*** | | |
*** | * | cummit CUMMIT_OBJECT_NAME
*** | |/  Author: A U Thor <author@example.com>
*** | |
*** | |       octopus-a
*** | |   ---
*** | |    octopus-a.t | 1 +
*** | |    1 file changed, 1 insertion(+)
*** | |
*** | |   diff --but a/octopus-a.t b/octopus-a.t
*** | |   new file mode 100644
*** | |   index BEFORE..AFTER
*** | |   --- /dev/null
*** | |   +++ b/octopus-a.t
*** | |   @@ -0,0 +1 @@
*** | |   +octopus-a
*** | |
*** * | cummit CUMMIT_OBJECT_NAME
*** |/  Author: A U Thor <author@example.com>
*** |
*** |       seventh
*** |   ---
*** |    seventh.t | 1 +
*** |    1 file changed, 1 insertion(+)
*** |
*** |   diff --but a/seventh.t b/seventh.t
*** |   new file mode 100644
*** |   index BEFORE..AFTER
*** |   --- /dev/null
*** |   +++ b/seventh.t
*** |   @@ -0,0 +1 @@
*** |   +seventh
*** |
*** *   cummit CUMMIT_OBJECT_NAME
*** |\  Merge: MERGE_PARENTS
*** | | Author: A U Thor <author@example.com>
*** | |
*** | |     Merge branch 'tangle'
*** | |
*** | *   cummit CUMMIT_OBJECT_NAME
*** | |\  Merge: MERGE_PARENTS
*** | | | Author: A U Thor <author@example.com>
*** | | |
*** | | |     Merge branch 'side' (early part) into tangle
*** | | |
*** | * |   cummit CUMMIT_OBJECT_NAME
*** | |\ \  Merge: MERGE_PARENTS
*** | | | | Author: A U Thor <author@example.com>
*** | | | |
*** | | | |     Merge branch 'main' (early part) into tangle
*** | | | |
*** | * | | cummit CUMMIT_OBJECT_NAME
*** | | | | Author: A U Thor <author@example.com>
*** | | | |
*** | | | |     tangle-a
*** | | | | ---
*** | | | |  tangle-a | 1 +
*** | | | |  1 file changed, 1 insertion(+)
*** | | | |
*** | | | | diff --but a/tangle-a b/tangle-a
*** | | | | new file mode 100644
*** | | | | index BEFORE..AFTER
*** | | | | --- /dev/null
*** | | | | +++ b/tangle-a
*** | | | | @@ -0,0 +1 @@
*** | | | | +a
*** | | | |
*** * | | |   cummit CUMMIT_OBJECT_NAME
*** |\ \ \ \  Merge: MERGE_PARENTS
*** | | | | | Author: A U Thor <author@example.com>
*** | | | | |
*** | | | | |     Merge branch 'side'
*** | | | | |
*** | * | | | cummit CUMMIT_OBJECT_NAME
*** | | |_|/  Author: A U Thor <author@example.com>
*** | |/| |
*** | | | |       side-2
*** | | | |   ---
*** | | | |    2 | 1 +
*** | | | |    1 file changed, 1 insertion(+)
*** | | | |
*** | | | |   diff --but a/2 b/2
*** | | | |   new file mode 100644
*** | | | |   index BEFORE..AFTER
*** | | | |   --- /dev/null
*** | | | |   +++ b/2
*** | | | |   @@ -0,0 +1 @@
*** | | | |   +2
*** | | | |
*** | * | | cummit CUMMIT_OBJECT_NAME
*** | | | | Author: A U Thor <author@example.com>
*** | | | |
*** | | | |     side-1
*** | | | | ---
*** | | | |  1 | 1 +
*** | | | |  1 file changed, 1 insertion(+)
*** | | | |
*** | | | | diff --but a/1 b/1
*** | | | | new file mode 100644
*** | | | | index BEFORE..AFTER
*** | | | | --- /dev/null
*** | | | | +++ b/1
*** | | | | @@ -0,0 +1 @@
*** | | | | +1
*** | | | |
*** * | | | cummit CUMMIT_OBJECT_NAME
*** | | | | Author: A U Thor <author@example.com>
*** | | | |
*** | | | |     Second
*** | | | | ---
*** | | | |  one | 1 +
*** | | | |  1 file changed, 1 insertion(+)
*** | | | |
*** | | | | diff --but a/one b/one
*** | | | | new file mode 100644
*** | | | | index BEFORE..AFTER
*** | | | | --- /dev/null
*** | | | | +++ b/one
*** | | | | @@ -0,0 +1 @@
*** | | | | +case
*** | | | |
*** * | | | cummit CUMMIT_OBJECT_NAME
*** | |_|/  Author: A U Thor <author@example.com>
*** |/| |
*** | | |       sixth
*** | | |   ---
*** | | |    a/two | 1 -
*** | | |    1 file changed, 1 deletion(-)
*** | | |
*** | | |   diff --but a/a/two b/a/two
*** | | |   deleted file mode 100644
*** | | |   index BEFORE..AFTER
*** | | |   --- a/a/two
*** | | |   +++ /dev/null
*** | | |   @@ -1 +0,0 @@
*** | | |   -ni
*** | | |
*** * | | cummit CUMMIT_OBJECT_NAME
*** | | | Author: A U Thor <author@example.com>
*** | | |
*** | | |     fifth
*** | | | ---
*** | | |  a/two | 1 +
*** | | |  1 file changed, 1 insertion(+)
*** | | |
*** | | | diff --but a/a/two b/a/two
*** | | | new file mode 100644
*** | | | index BEFORE..AFTER
*** | | | --- /dev/null
*** | | | +++ b/a/two
*** | | | @@ -0,0 +1 @@
*** | | | +ni
*** | | |
*** * | | cummit CUMMIT_OBJECT_NAME
*** |/ /  Author: A U Thor <author@example.com>
*** | |
*** | |       fourth
*** | |   ---
*** | |    ein | 1 +
*** | |    1 file changed, 1 insertion(+)
*** | |
*** | |   diff --but a/ein b/ein
*** | |   new file mode 100644
*** | |   index BEFORE..AFTER
*** | |   --- /dev/null
*** | |   +++ b/ein
*** | |   @@ -0,0 +1 @@
*** | |   +ichi
*** | |
*** * | cummit CUMMIT_OBJECT_NAME
*** |/  Author: A U Thor <author@example.com>
*** |
*** |       third
*** |   ---
*** |    ichi | 1 +
*** |    one  | 1 -
*** |    2 files changed, 1 insertion(+), 1 deletion(-)
*** |
*** |   diff --but a/ichi b/ichi
*** |   new file mode 100644
*** |   index BEFORE..AFTER
*** |   --- /dev/null
*** |   +++ b/ichi
*** |   @@ -0,0 +1 @@
*** |   +ichi
*** |   diff --but a/one b/one
*** |   deleted file mode 100644
*** |   index BEFORE..AFTER
*** |   --- a/one
*** |   +++ /dev/null
*** |   @@ -1 +0,0 @@
*** |   -ichi
*** |
*** * cummit CUMMIT_OBJECT_NAME
*** | Author: A U Thor <author@example.com>
*** |
*** |     second
*** | ---
*** |  one | 2 +-
*** |  1 file changed, 1 insertion(+), 1 deletion(-)
*** |
*** | diff --but a/one b/one
*** | index BEFORE..AFTER 100644
*** | --- a/one
*** | +++ b/one
*** | @@ -1 +1 @@
*** | -one
*** | +ichi
*** |
*** * cummit CUMMIT_OBJECT_NAME
***   Author: A U Thor <author@example.com>
***
***       initial
***   ---
***    one | 1 +
***    1 file changed, 1 insertion(+)
***
***   diff --but a/one b/one
***   new file mode 100644
***   index BEFORE..AFTER
***   --- /dev/null
***   +++ b/one
***   @@ -0,0 +1 @@
***   +one
EOF

test_expect_success 'log --line-prefix="*** " --graph with diff and stats' '
	lib_test_cmp_short_graph --line-prefix="*** " --no-renames --stat -p
'

cat >expect <<-\EOF
* reach
|
| A	reach.t
* Merge branch 'tangle'
*   Merge branch 'side'
|\
| * side-2
|
|   A	2
* Second
|
| A	one
* sixth

  D	a/two
EOF

test_expect_success 'log --graph with --name-status' '
	test_cmp_graph --name-status tangle..reach
'

cat >expect <<-\EOF
* reach
|
| reach.t
* Merge branch 'tangle'
*   Merge branch 'side'
|\
| * side-2
|
|   2
* Second
|
| one
* sixth

  a/two
EOF

test_expect_success 'log --graph with --name-only' '
	test_cmp_graph --name-only tangle..reach
'

test_expect_success '--no-graph countermands --graph' '
	but log >expect &&
	but log --graph --no-graph >actual &&
	test_cmp expect actual
'

test_expect_success '--graph countermands --no-graph' '
	but log --graph >expect &&
	but log --no-graph --graph >actual &&
	test_cmp expect actual
'

test_expect_success '--no-graph does not unset --topo-order' '
	but log --topo-order >expect &&
	but log --topo-order --no-graph >actual &&
	test_cmp expect actual
'

test_expect_success '--no-graph does not unset --parents' '
	but log --parents >expect &&
	but log --parents --no-graph >actual &&
	test_cmp expect actual
'

test_expect_success '--reverse and --graph conflict' '
	test_must_fail but log --reverse --graph 2>stderr &&
	test_i18ngrep "cannot be used together" stderr
'

test_expect_success '--reverse --graph --no-graph works' '
	but log --reverse >expect &&
	but log --reverse --graph --no-graph >actual &&
	test_cmp expect actual
'

test_expect_success '--show-linear-break and --graph conflict' '
	test_must_fail but log --show-linear-break --graph 2>stderr &&
	test_i18ngrep "cannot be used together" stderr
'

test_expect_success '--show-linear-break --graph --no-graph works' '
	but log --show-linear-break >expect &&
	but log --show-linear-break --graph --no-graph >actual &&
	test_cmp expect actual
'

test_expect_success '--no-walk and --graph conflict' '
	test_must_fail but log --no-walk --graph 2>stderr &&
	test_i18ngrep "cannot be used together" stderr
'

test_expect_success '--no-walk --graph --no-graph works' '
	but log --no-walk >expect &&
	but log --no-walk --graph --no-graph >actual &&
	test_cmp expect actual
'

test_expect_success '--walk-reflogs and --graph conflict' '
	test_must_fail but log --walk-reflogs --graph 2>stderr &&
	(test_i18ngrep "cannot combine" stderr ||
		test_i18ngrep "cannot be used together" stderr)
'

test_expect_success '--walk-reflogs --graph --no-graph works' '
	but log --walk-reflogs >expect &&
	but log --walk-reflogs --graph --no-graph >actual &&
	test_cmp expect actual
'

test_expect_success 'dotdot is a parent directory' '
	mkdir -p a/b &&
	( echo sixth && echo fifth ) >expect &&
	( cd a/b && but log --format=%s .. ) >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'setup signed branch' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b signed main &&
	echo foo >foo &&
	but add foo &&
	but cummit -S -m signed_cummit
'

test_expect_success GPG 'setup signed branch with subkey' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b signed-subkey main &&
	echo foo >foo &&
	but add foo &&
	but cummit -SB7227189 -m signed_cummit
'

test_expect_success GPGSM 'setup signed branch x509' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b signed-x509 main &&
	echo foo >foo &&
	but add foo &&
	test_config gpg.format x509 &&
	test_config user.signingkey $BUT_CUMMITTER_EMAIL &&
	but cummit -S -m signed_cummit
'

test_expect_success GPGSSH 'setup sshkey signed branch' '
	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b signed-ssh main &&
	echo foo >foo &&
	but add foo &&
	but cummit -S -m signed_cummit
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'create signed cummits with keys having defined lifetimes' '
	test_config gpg.format ssh &&
	touch file &&
	but add file &&

	echo expired >file && test_tick && but cummit -a -m expired -S"${GPGSSH_KEY_EXPIRED}" &&
	but tag expired-signed &&

	echo notyetvalid >file && test_tick && but cummit -a -m notyetvalid -S"${GPGSSH_KEY_NOTYETVALID}" &&
	but tag notyetvalid-signed &&

	echo timeboxedvalid >file && test_tick && but cummit -a -m timeboxedvalid -S"${GPGSSH_KEY_TIMEBOXEDVALID}" &&
	but tag timeboxedvalid-signed &&

	echo timeboxedinvalid >file && test_tick && but cummit -a -m timeboxedinvalid -S"${GPGSSH_KEY_TIMEBOXEDINVALID}" &&
	but tag timeboxedinvalid-signed
'

test_expect_success GPGSM 'log x509 fingerprint' '
	echo "F8BF62E0693D0694816377099909C779FA23FD65 | " >expect &&
	but log -n1 --format="%GF | %GP" signed-x509 >actual &&
	test_cmp expect actual
'

test_expect_success GPGSM 'log OpenPGP fingerprint' '
	echo "D4BE22311AD3131E5EDA29A461092E85B7227189" > expect &&
	but log -n1 --format="%GP" signed-subkey >actual &&
	test_cmp expect actual
'

test_expect_success GPGSSH 'log ssh key fingerprint' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	ssh-keygen -lf  "${GPGSSH_KEY_PRIMARY}" | awk "{print \$2\" | \"}" >expect &&
	but log -n1 --format="%GF | %GP" signed-ssh >actual &&
	test_cmp expect actual
'

test_expect_success GPG 'log --graph --show-signature' '
	but log --graph --show-signature -n1 signed >actual &&
	grep "^| gpg: Signature made" actual &&
	grep "^| gpg: Good signature" actual
'

test_expect_success GPGSM 'log --graph --show-signature x509' '
	but log --graph --show-signature -n1 signed-x509 >actual &&
	grep "^| gpgsm: Signature made" actual &&
	grep "^| gpgsm: Good signature" actual
'

test_expect_success GPGSSH 'log --graph --show-signature ssh' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but log --graph --show-signature -n1 signed-ssh >actual &&
	grep "${GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'log shows failure on expired signature key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but log --graph --show-signature -n1 expired-signed >actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'log shows failure on not yet valid signature key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but log --graph --show-signature -n1 notyetvalid-signed >actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'log show success with cummit date and key validity matching' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but log --graph --show-signature -n1 timeboxedvalid-signed >actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'log shows failure with cummit date outside of key validity' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but log --graph --show-signature -n1 timeboxedinvalid-signed >actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPG 'log --graph --show-signature for merged tag' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b plain main &&
	echo aaa >bar &&
	but add bar &&
	but cummit -m bar_cummit &&
	but checkout -b tagged main &&
	echo bbb >baz &&
	but add baz &&
	but cummit -m baz_cummit &&
	but tag -s -m signed_tag_msg signed_tag &&
	but checkout plain &&
	but merge --no-ff -m msg signed_tag &&
	but log --graph --show-signature -n1 plain >actual &&
	grep "^|\\\  merged tag" actual &&
	grep "^| | gpg: Signature made" actual &&
	grep "^| | gpg: Good signature" actual
'

test_expect_success GPG 'log --graph --show-signature for merged tag in shallow clone' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b plain-shallow main &&
	echo aaa >bar &&
	but add bar &&
	but cummit -m bar_cummit &&
	but checkout --detach main &&
	echo bbb >baz &&
	but add baz &&
	but cummit -m baz_cummit &&
	but tag -s -m signed_tag_msg signed_tag_shallow &&
	hash=$(but rev-parse HEAD) &&
	but checkout plain-shallow &&
	but merge --no-ff -m msg signed_tag_shallow &&
	but clone --depth 1 --no-local . shallow &&
	test_when_finished "rm -rf shallow" &&
	but -C shallow log --graph --show-signature -n1 plain-shallow >actual &&
	grep "tag signed_tag_shallow names a non-parent $hash" actual
'

test_expect_success GPG 'log --graph --show-signature for merged tag with missing key' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b plain-nokey main &&
	echo aaa >bar &&
	but add bar &&
	but cummit -m bar_cummit &&
	but checkout -b tagged-nokey main &&
	echo bbb >baz &&
	but add baz &&
	but cummit -m baz_cummit &&
	but tag -s -m signed_tag_msg signed_tag_nokey &&
	but checkout plain-nokey &&
	but merge --no-ff -m msg signed_tag_nokey &&
	GNUPGHOME=. but log --graph --show-signature -n1 plain-nokey >actual &&
	grep "^|\\\  merged tag" actual &&
	grep "^| | gpg: Signature made" actual &&
	grep -E "^| | gpg: Can'"'"'t check signature: (public key not found|No public key)" actual
'

test_expect_success GPG 'log --graph --show-signature for merged tag with bad signature' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b plain-bad main &&
	echo aaa >bar &&
	but add bar &&
	but cummit -m bar_cummit &&
	but checkout -b tagged-bad main &&
	echo bbb >baz &&
	but add baz &&
	but cummit -m baz_cummit &&
	but tag -s -m signed_tag_msg signed_tag_bad &&
	but cat-file tag signed_tag_bad >raw &&
	sed -e "s/signed_tag_msg/forged/" raw >forged &&
	but hash-object -w -t tag forged >forged.tag &&
	but checkout plain-bad &&
	but merge --no-ff -m msg "$(cat forged.tag)" &&
	but log --graph --show-signature -n1 plain-bad >actual &&
	grep "^|\\\  merged tag" actual &&
	grep "^| | gpg: Signature made" actual &&
	grep "^| | gpg: BAD signature from" actual
'

test_expect_success GPG 'log --show-signature for merged tag with GPG failure' '
	test_when_finished "but reset --hard && but checkout main" &&
	but checkout -b plain-fail main &&
	echo aaa >bar &&
	but add bar &&
	but cummit -m bar_cummit &&
	but checkout -b tagged-fail main &&
	echo bbb >baz &&
	but add baz &&
	but cummit -m baz_cummit &&
	but tag -s -m signed_tag_msg signed_tag_fail &&
	but checkout plain-fail &&
	but merge --no-ff -m msg signed_tag_fail &&
	TMPDIR="$(pwd)/bogus" but log --show-signature -n1 plain-fail >actual &&
	grep "^merged tag" actual &&
	grep "^No signature" actual &&
	! grep "^gpg: Signature made" actual
'

test_expect_success GPGSM 'log --graph --show-signature for merged tag x509' '
	test_when_finished "but reset --hard && but checkout main" &&
	test_config gpg.format x509 &&
	test_config user.signingkey $BUT_CUMMITTER_EMAIL &&
	but checkout -b plain-x509 main &&
	echo aaa >bar &&
	but add bar &&
	but cummit -m bar_cummit &&
	but checkout -b tagged-x509 main &&
	echo bbb >baz &&
	but add baz &&
	but cummit -m baz_cummit &&
	but tag -s -m signed_tag_msg signed_tag_x509 &&
	but checkout plain-x509 &&
	but merge --no-ff -m msg signed_tag_x509 &&
	but log --graph --show-signature -n1 plain-x509 >actual &&
	grep "^|\\\  merged tag" actual &&
	grep "^| | gpgsm: Signature made" actual &&
	grep "^| | gpgsm: Good signature" actual
'

test_expect_success GPGSM 'log --graph --show-signature for merged tag x509 missing key' '
	test_when_finished "but reset --hard && but checkout main" &&
	test_config gpg.format x509 &&
	test_config user.signingkey $BUT_CUMMITTER_EMAIL &&
	but checkout -b plain-x509-nokey main &&
	echo aaa >bar &&
	but add bar &&
	but cummit -m bar_cummit &&
	but checkout -b tagged-x509-nokey main &&
	echo bbb >baz &&
	but add baz &&
	but cummit -m baz_cummit &&
	but tag -s -m signed_tag_msg signed_tag_x509_nokey &&
	but checkout plain-x509-nokey &&
	but merge --no-ff -m msg signed_tag_x509_nokey &&
	GNUPGHOME=. but log --graph --show-signature -n1 plain-x509-nokey >actual &&
	grep "^|\\\  merged tag" actual &&
	grep -e "^| | gpgsm: certificate not found" \
	     -e "^| | gpgsm: failed to find the certificate: Not found" actual
'

test_expect_success GPGSM 'log --graph --show-signature for merged tag x509 bad signature' '
	test_when_finished "but reset --hard && but checkout main" &&
	test_config gpg.format x509 &&
	test_config user.signingkey $BUT_CUMMITTER_EMAIL &&
	but checkout -b plain-x509-bad main &&
	echo aaa >bar &&
	but add bar &&
	but cummit -m bar_cummit &&
	but checkout -b tagged-x509-bad main &&
	echo bbb >baz &&
	but add baz &&
	but cummit -m baz_cummit &&
	but tag -s -m signed_tag_msg signed_tag_x509_bad &&
	but cat-file tag signed_tag_x509_bad >raw &&
	sed -e "s/signed_tag_msg/forged/" raw >forged &&
	but hash-object -w -t tag forged >forged.tag &&
	but checkout plain-x509-bad &&
	but merge --no-ff -m msg "$(cat forged.tag)" &&
	but log --graph --show-signature -n1 plain-x509-bad >actual &&
	grep "^|\\\  merged tag" actual &&
	grep "^| | gpgsm: Signature made" actual &&
	grep "^| | gpgsm: invalid signature" actual
'


test_expect_success GPG '--no-show-signature overrides --show-signature' '
	but log -1 --show-signature --no-show-signature signed >actual &&
	! grep "^gpg:" actual
'

test_expect_success GPG 'log.showsignature=true behaves like --show-signature' '
	test_config log.showsignature true &&
	but log -1 signed >actual &&
	grep "gpg: Signature made" actual &&
	grep "gpg: Good signature" actual
'

test_expect_success GPG '--no-show-signature overrides log.showsignature=true' '
	test_config log.showsignature true &&
	but log -1 --no-show-signature signed >actual &&
	! grep "^gpg:" actual
'

test_expect_success GPG '--show-signature overrides log.showsignature=false' '
	test_config log.showsignature false &&
	but log -1 --show-signature signed >actual &&
	grep "gpg: Signature made" actual &&
	grep "gpg: Good signature" actual
'

test_expect_success 'log --graph --no-walk is forbidden' '
	test_must_fail but log --graph --no-walk
'

test_expect_success 'log on empty repo fails' '
	but init empty &&
	test_when_finished "rm -rf empty" &&
	test_must_fail but -C empty log 2>stderr &&
	test_i18ngrep does.not.have.any.cummits stderr
'

test_expect_success REFFILES 'log diagnoses bogus HEAD hash' '
	but init empty &&
	test_when_finished "rm -rf empty" &&
	echo 1234abcd >empty/.but/refs/heads/main &&
	test_must_fail but -C empty log 2>stderr &&
	test_i18ngrep broken stderr
'

test_expect_success 'log diagnoses bogus HEAD symref' '
	but init empty &&
	but --but-dir empty/.but symbolic-ref HEAD refs/heads/invalid.lock &&
	test_must_fail but -C empty log 2>stderr &&
	test_i18ngrep broken stderr &&
	test_must_fail but -C empty log --default totally-bogus 2>stderr &&
	test_i18ngrep broken stderr
'

test_expect_success 'log does not default to HEAD when rev input is given' '
	but log --branches=does-not-exist >actual &&
	test_must_be_empty actual
'

test_expect_success 'do not default to HEAD with ignored object on cmdline' '
	but log --ignore-missing $ZERO_OID >actual &&
	test_must_be_empty actual
'

test_expect_success 'do not default to HEAD with ignored object on stdin' '
	echo $ZERO_OID | but log --ignore-missing --stdin >actual &&
	test_must_be_empty actual
'

test_expect_success 'set up --source tests' '
	but checkout --orphan source-a &&
	test_cummit one &&
	test_cummit two &&
	but checkout -b source-b HEAD^ &&
	test_cummit three
'

test_expect_success 'log --source paints branch names' '
	cat >expect <<-EOF &&
	$(but rev-parse --short :/three)	source-b three
	$(but rev-parse --short :/two  )	source-a two
	$(but rev-parse --short :/one  )	source-b one
	EOF
	but log --oneline --source source-a source-b >actual &&
	test_cmp expect actual
'

test_expect_success 'log --source paints tag names' '
	but tag -m tagged source-tag &&
	cat >expect <<-EOF &&
	$(but rev-parse --short :/three)	source-tag three
	$(but rev-parse --short :/two  )	source-a two
	$(but rev-parse --short :/one  )	source-tag one
	EOF
	but log --oneline --source source-tag source-a >actual &&
	test_cmp expect actual
'

test_expect_success 'log --source paints symmetric ranges' '
	cat >expect <<-EOF &&
	$(but rev-parse --short :/three)	source-b three
	$(but rev-parse --short :/two  )	source-a two
	EOF
	but log --oneline --source source-a...source-b >actual &&
	test_cmp expect actual
'

test_expect_success '--exclude-promisor-objects does not BUG-crash' '
	test_must_fail but log --exclude-promisor-objects source-a
'

test_expect_success 'log --decorate includes all levels of tag annotated tags' '
	but checkout -b branch &&
	but cummit --allow-empty -m "new cummit" &&
	but tag lightweight HEAD &&
	but tag -m annotated annotated HEAD &&
	but tag -m double-0 double-0 HEAD &&
	but tag -m double-1 double-1 double-0 &&
	cat >expect <<-\EOF &&
	HEAD -> branch, tag: lightweight, tag: double-1, tag: double-0, tag: annotated
	EOF
	but log -1 --format="%D" >actual &&
	test_cmp expect actual
'

test_expect_success 'log --end-of-options' '
       but update-ref refs/heads/--source HEAD &&
       but log --end-of-options --source >actual &&
       but log >expect &&
       test_cmp expect actual
'

test_expect_success 'set up cummits with different authors' '
	but checkout --orphan authors &&
	test_cummit --author "Jim <jim@example.com>" jim_1 &&
	test_cummit --author "Val <val@example.com>" val_1 &&
	test_cummit --author "Val <val@example.com>" val_2 &&
	test_cummit --author "Jim <jim@example.com>" jim_2 &&
	test_cummit --author "Val <val@example.com>" val_3 &&
	test_cummit --author "Jim <jim@example.com>" jim_3
'

test_expect_success 'log --invert-grep --grep --author' '
	cat >expect <<-\EOF &&
	val_3
	val_1
	EOF
	but log --format=%s --author=Val --grep 2 --invert-grep >actual &&
	test_cmp expect actual
'

test_done
