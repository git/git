#!/bin/sh
#
# Copyright (c) 2010, Will Palmer
# Copyright (c) 2011, Alexey Shumkin (+ non-UTF-8 commit encoding tests)
#

test_description='Test pretty formats'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Tested non-UTF-8 encoding
test_encoding="ISO8859-1"

sample_utf8_part=$(printf "f\303\244ng")

commit_msg () {
	# String "initial. initial" partly in German
	# (translated with Google Translate),
	# encoded in UTF-8, used as a commit log message below.
	msg="initial. an${sample_utf8_part}lich\n"
	if test -n "$1"
	then
		printf "$msg" | iconv -f utf-8 -t "$1"
	else
		printf "$msg"
	fi
}

test_expect_success 'set up basic repos' '
	>foo &&
	>bar &&
	git add foo &&
	test_tick &&
	test_config i18n.commitEncoding $test_encoding &&
	commit_msg $test_encoding | git commit -F - &&
	git add bar &&
	test_tick &&
	git commit -m "add bar"
'

test_expect_success 'alias builtin format' '
	git log --pretty=oneline >expected &&
	test_config pretty.test-alias oneline &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias masking builtin format' '
	git log --pretty=oneline >expected &&
	test_config pretty.oneline "%H" &&
	git log --pretty=oneline >actual &&
	test_cmp expected actual
'

test_expect_success 'alias user-defined format' '
	git log --pretty="format:%h" >expected &&
	test_config pretty.test-alias "format:%h" &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias user-defined format is matched case-insensitively' '
	git log --pretty="format:%h" >expected &&
	test_config pretty.testone "format:%h" &&
	test_config pretty.testtwo testOne &&
	git log --pretty=testTwo >actual &&
	test_cmp expected actual
'

test_expect_success 'alias user-defined tformat with %s (ISO8859-1 encoding)' '
	test_config i18n.logOutputEncoding $test_encoding &&
	git log --oneline >expected-s &&
	git log --pretty="tformat:%h %s" >actual-s &&
	test_cmp expected-s actual-s
'

test_expect_success 'alias user-defined tformat with %s (utf-8 encoding)' '
	git log --oneline >expected-s &&
	git log --pretty="tformat:%h %s" >actual-s &&
	test_cmp expected-s actual-s
'

test_expect_success 'alias user-defined tformat' '
	git log --pretty="tformat:%h" >expected &&
	test_config pretty.test-alias "tformat:%h" &&
	git log --pretty=test-alias >actual &&
	test_cmp expected actual
'

test_expect_success 'alias non-existent format' '
	test_config pretty.test-alias format-that-will-never-exist &&
	test_must_fail git log --pretty=test-alias
'

test_expect_success 'alias of an alias' '
	git log --pretty="tformat:%h" >expected &&
	test_config pretty.test-foo "tformat:%h" &&
	test_config pretty.test-bar test-foo &&
	git log --pretty=test-bar >actual && test_cmp expected actual
'

test_expect_success 'alias masking an alias' '
	git log --pretty=format:"Two %H" >expected &&
	test_config pretty.duplicate "format:One %H" &&
	test_config pretty.duplicate "format:Two %H" --add &&
	git log --pretty=duplicate >actual &&
	test_cmp expected actual
'

test_expect_success 'alias loop' '
	test_config pretty.test-foo test-bar &&
	test_config pretty.test-bar test-foo &&
	test_must_fail git log --pretty=test-foo
'

test_expect_success ICONV 'NUL separation' '
	printf "add bar\0$(commit_msg)" >expected &&
	git log -z --pretty="format:%s" >actual &&
	test_cmp expected actual
'

test_expect_success ICONV 'NUL termination' '
	printf "add bar\0$(commit_msg)\0" >expected &&
	git log -z --pretty="tformat:%s" >actual &&
	test_cmp expected actual
'

test_expect_success ICONV 'NUL separation with --stat' '
	stat0_part=$(git diff --stat HEAD^ HEAD) &&
	stat1_part=$(git diff-tree --no-commit-id --stat --root HEAD^) &&
	printf "add bar\n$stat0_part\n\0$(commit_msg)\n$stat1_part\n" >expected &&
	git log -z --stat --pretty="format:%s" >actual &&
	test_cmp expected actual
'

test_expect_failure 'NUL termination with --stat' '
	stat0_part=$(git diff --stat HEAD^ HEAD) &&
	stat1_part=$(git diff-tree --no-commit-id --stat --root HEAD^) &&
	printf "add bar\n$stat0_part\n\0$(commit_msg)\n$stat1_part\n\0" >expected &&
	git log -z --stat --pretty="tformat:%s" >actual &&
	test_cmp expected actual
'

for p in short medium full fuller email raw
do
	test_expect_success "NUL termination with --reflog --pretty=$p" '
		revs="$(git rev-list --reflog)" &&
		for r in $revs
		do
			git show -s "$r" --pretty="$p" &&
			printf "\0" || return 1
		done >expect &&
		{
			git log -z --reflog --pretty="$p" &&
			printf "\0"
		} >actual &&
		test_cmp expect actual
	'
done

test_expect_success 'NUL termination with --reflog --pretty=oneline' '
	revs="$(git rev-list --reflog)" &&
	for r in $revs
	do
		git show -s --pretty=oneline "$r" >raw &&
		lf_to_nul <raw || return 1
	done >expect &&
	# the trailing NUL is already produced so we do not need to
	# output another one
	git log -z --pretty=oneline --reflog >actual &&
	test_cmp expect actual
'

test_expect_success 'setup more commits' '
	test_commit "message one" one one message-one &&
	test_commit "message two" two two message-two &&
	head1=$(git rev-parse --verify --short HEAD~0) &&
	head2=$(git rev-parse --verify --short HEAD~1) &&
	head3=$(git rev-parse --verify --short HEAD~2) &&
	head4=$(git rev-parse --verify --short HEAD~3)
'

test_expect_success ICONV 'left alignment formatting' '
	git log --pretty="tformat:%<(40)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	message two                            Z
	message one                            Z
	add bar                                Z
	$(commit_msg)                    Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%<(40)%s" >actual &&
	qz_to_tab_space <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	message two                            Z
	message one                            Z
	add bar                                Z
	$(commit_msg)                    Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting at the nth column' '
	git log --pretty="tformat:%h %<|(40)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	$head1 message two                    Z
	$head2 message one                    Z
	$head3 add bar                        Z
	$head4 $(commit_msg)            Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting at the nth column' '
	COLUMNS=50 git log --pretty="tformat:%h %<|(-10)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	$head1 message two                    Z
	$head2 message one                    Z
	$head3 add bar                        Z
	$head4 $(commit_msg)            Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting at the nth column. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%h %<|(40)%s" >actual &&
	qz_to_tab_space <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	$head1 message two                    Z
	$head2 message one                    Z
	$head3 add bar                        Z
	$head4 $(commit_msg)            Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting with no padding' '
	git log --pretty="tformat:%<(1)%s" >actual &&
	cat <<-EOF >expected &&
	message two
	message one
	add bar
	$(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success 'left alignment formatting with no padding. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%<(1)%s" >actual &&
	cat <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	message two
	message one
	add bar
	$(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting with trunc' '
	git log --pretty="tformat:%<(10,trunc)%s" >actual &&
	qz_to_tab_space <<-\EOF >expected &&
	message ..
	message ..
	add bar  Z
	initial...
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting with trunc. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%<(10,trunc)%s" >actual &&
	qz_to_tab_space <<-\EOF | iconv -f utf-8 -t $test_encoding >expected &&
	message ..
	message ..
	add bar  Z
	initial...
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting with ltrunc' '
	git log --pretty="tformat:%<(10,ltrunc)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	..sage two
	..sage one
	add bar  Z
	..${sample_utf8_part}lich
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting with ltrunc. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%<(10,ltrunc)%s" >actual &&
	qz_to_tab_space <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	..sage two
	..sage one
	add bar  Z
	..${sample_utf8_part}lich
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting with mtrunc' '
	git log --pretty="tformat:%<(10,mtrunc)%s" >actual &&
	qz_to_tab_space <<-\EOF >expected &&
	mess.. two
	mess.. one
	add bar  Z
	init..lich
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left alignment formatting with mtrunc. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%<(10,mtrunc)%s" >actual &&
	qz_to_tab_space <<-\EOF | iconv -f utf-8 -t $test_encoding >expected &&
	mess.. two
	mess.. one
	add bar  Z
	init..lich
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'right alignment formatting' '
	git log --pretty="tformat:%>(40)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	Z                            message two
	Z                            message one
	Z                                add bar
	Z                    $(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'right alignment formatting. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%>(40)%s" >actual &&
	qz_to_tab_space <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	Z                            message two
	Z                            message one
	Z                                add bar
	Z                    $(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'right alignment formatting at the nth column' '
	git log --pretty="tformat:%h %>|(40)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	$head1                      message two
	$head2                      message one
	$head3                          add bar
	$head4              $(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'right alignment formatting at the nth column' '
	COLUMNS=50 git log --pretty="tformat:%h %>|(-10)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	$head1                      message two
	$head2                      message one
	$head3                          add bar
	$head4              $(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'right alignment formatting at the nth column. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%h %>|(40)%s" >actual &&
	qz_to_tab_space <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	$head1                      message two
	$head2                      message one
	$head3                          add bar
	$head4              $(commit_msg)
	EOF
	test_cmp expected actual
'

# Note: Space between 'message' and 'two' should be in the same column
# as in previous test.
test_expect_success ICONV 'right alignment formatting at the nth column with --graph. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --graph --pretty="tformat:%h %>|(40)%s" >actual &&
	iconv -f utf-8 -t $test_encoding >expected <<-EOF &&
	* $head1                    message two
	* $head2                    message one
	* $head3                        add bar
	* $head4            $(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'right alignment formatting with no padding' '
	git log --pretty="tformat:%>(1)%s" >actual &&
	cat <<-EOF >expected &&
	message two
	message one
	add bar
	$(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'right alignment formatting with no padding and with --graph' '
	git log --graph --pretty="tformat:%>(1)%s" >actual &&
	cat <<-EOF >expected &&
	* message two
	* message one
	* add bar
	* $(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'right alignment formatting with no padding. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%>(1)%s" >actual &&
	cat <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	message two
	message one
	add bar
	$(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'center alignment formatting' '
	git log --pretty="tformat:%><(40)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	Z             message two              Z
	Z             message one              Z
	Z               add bar                Z
	Z         $(commit_msg)          Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'center alignment formatting. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%><(40)%s" >actual &&
	qz_to_tab_space <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	Z             message two              Z
	Z             message one              Z
	Z               add bar                Z
	Z         $(commit_msg)          Z
	EOF
	test_cmp expected actual
'
test_expect_success ICONV 'center alignment formatting at the nth column' '
	git log --pretty="tformat:%h %><|(40)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	$head1           message two          Z
	$head2           message one          Z
	$head3             add bar            Z
	$head4       $(commit_msg)      Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'center alignment formatting at the nth column' '
	COLUMNS=70 git log --pretty="tformat:%h %><|(-30)%s" >actual &&
	qz_to_tab_space <<-EOF >expected &&
	$head1           message two          Z
	$head2           message one          Z
	$head3             add bar            Z
	$head4       $(commit_msg)      Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'center alignment formatting at the nth column. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%h %><|(40)%s" >actual &&
	qz_to_tab_space <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	$head1           message two          Z
	$head2           message one          Z
	$head3             add bar            Z
	$head4       $(commit_msg)      Z
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'center alignment formatting with no padding' '
	git log --pretty="tformat:%><(1)%s" >actual &&
	cat <<-EOF >expected &&
	message two
	message one
	add bar
	$(commit_msg)
	EOF
	test_cmp expected actual
'

# save HEAD's SHA-1 digest (with no abbreviations) to use it below
# as far as the next test amends HEAD
old_head1=$(git rev-parse --verify HEAD~0)
test_expect_success ICONV 'center alignment formatting with no padding. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%><(1)%s" >actual &&
	cat <<-EOF | iconv -f utf-8 -t $test_encoding >expected &&
	message two
	message one
	add bar
	$(commit_msg)
	EOF
	test_cmp expected actual
'

test_expect_success ICONV 'left/right alignment formatting with stealing' '
	git commit --amend -m short --author "long long long <long@me.com>" &&
	git log --pretty="tformat:%<(10,trunc)%s%>>(10,ltrunc)% an" >actual &&
	cat <<-\EOF >expected &&
	short long  long long
	message ..   A U Thor
	add bar      A U Thor
	initial...   A U Thor
	EOF
	test_cmp expected actual
'
test_expect_success ICONV 'left/right alignment formatting with stealing. i18n.logOutputEncoding' '
	git -c i18n.logOutputEncoding=$test_encoding log --pretty="tformat:%<(10,trunc)%s%>>(10,ltrunc)% an" >actual &&
	cat <<-\EOF | iconv -f utf-8 -t $test_encoding >expected &&
	short long  long long
	message ..   A U Thor
	add bar      A U Thor
	initial...   A U Thor
	EOF
	test_cmp expected actual
'

test_expect_success 'strbuf_utf8_replace() not producing NUL' '
	git log --color --pretty="tformat:%<(10,trunc)%s%>>(10,ltrunc)%C(auto)%d" |
		test_decode_color |
		nul_to_q >actual &&
	! grep Q actual
'

# --date=[XXX] and corresponding %a[X] %c[X] format equivalency
test_expect_success '--date=iso-strict %ad%cd is the same as %aI%cI' '
	git log --format=%ad%n%cd --date=iso-strict >expected &&
	git log --format=%aI%n%cI >actual &&
	test_cmp expected actual
'

test_expect_success '--date=short %ad%cd is the same as %as%cs' '
	git log --format=%ad%n%cd --date=short >expected &&
	git log --format=%as%n%cs >actual &&
	test_cmp expected actual
'

test_expect_success '--date=human %ad%cd is the same as %ah%ch' '
	git log --format=%ad%n%cd --date=human >expected &&
	git log --format=%ah%n%ch >actual &&
	test_cmp expected actual
'

# get new digests (with no abbreviations)
test_expect_success 'set up log decoration tests' '
	head1=$(git rev-parse --verify HEAD~0) &&
	head2=$(git rev-parse --verify HEAD~1)
'

test_expect_success 'log decoration properly follows tag chain' '
	git tag -a tag1 -m tag1 &&
	git tag -a tag2 -m tag2 tag1 &&
	git tag -d tag1 &&
	git commit --amend -m shorter &&
	git log --no-walk --tags --pretty="%H %d" --decorate=full >actual &&
	if test_have_prereq ICONV
	then
		cat <<-EOF >expected
		$head2  (tag: refs/tags/message-one)
		$old_head1  (tag: refs/tags/message-two)
		$head1  (tag: refs/tags/tag2)
		EOF
	else
		cat <<-EOF >expected
		$head2  (tag: refs/tags/message-one)
		$old_head1  (tag: refs/tags/tag2, tag: refs/tags/message-two)
		EOF
	fi &&
	sort -k3 actual >actual1 &&
	test_cmp expected actual1
'

test_expect_success 'clean log decoration' '
	git log --no-walk --tags --pretty="%H %D" --decorate=full >actual &&
	if test_have_prereq ICONV
	then
		cat <<-EOF >expected
		$head2 tag: refs/tags/message-one
		$old_head1 tag: refs/tags/message-two
		$head1 tag: refs/tags/tag2
		EOF
	else
		cat <<-EOF >expected
		$head2 tag: refs/tags/message-one
		$old_head1 tag: refs/tags/tag2, tag: refs/tags/message-two
		EOF
	fi &&
	sort -k3 actual >actual1 &&
	test_cmp expected actual1
'

test_expect_success 'pretty format %decorate' '
	git checkout -b foo &&
	git commit --allow-empty -m "new commit" &&
	git tag bar &&
	git branch qux &&

	echo " (HEAD -> foo, tag: bar, qux)" >expect1 &&
	git log --format="%(decorate)" -1 >actual1 &&
	test_cmp expect1 actual1 &&

	echo "HEAD -> foo, tag: bar, qux" >expect2 &&
	git log --format="%(decorate:prefix=,suffix=)" -1 >actual2 &&
	test_cmp expect2 actual2 &&

	echo "[ bar; qux; foo ]" >expect3 &&
	git log --format="%(decorate:prefix=[ ,suffix= ],separator=%x3B ,tag=)" \
		--decorate-refs=refs/ -1 >actual3 &&
	test_cmp expect3 actual3 &&

	# Try with a typo (in "separator"), in which case the placeholder should
	# not be replaced.
	echo "%(decorate:prefix=[ ,suffix= ],separater=; )" >expect4 &&
	git log --format="%(decorate:prefix=[ ,suffix= ],separater=%x3B )" \
		-1 >actual4 &&
	test_cmp expect4 actual4 &&

	echo "HEAD->foo bar qux" >expect5 &&
	git log --format="%(decorate:prefix=,suffix=,separator= ,tag=,pointer=->)" \
		-1 >actual5 &&
	test_cmp expect5 actual5
'

cat >trailers <<EOF
Signed-off-by: A U Thor <author@example.com>
Acked-by: A U Thor <author@example.com>
[ v2 updated patch description ]
Signed-off-by: A U Thor
  <author@example.com>
EOF

unfold () {
	perl -0pe 's/\n\s+/ /g'
}

test_expect_success 'set up trailer tests' '
	echo "Some contents" >trailerfile &&
	git add trailerfile &&
	git commit -F - <<-EOF
	trailers: this commit message has trailers

	This commit is a test commit with trailers at the end. We parse this
	message and display the trailers using %(trailers).

	$(cat trailers)
	EOF
'

test_expect_success 'pretty format %(trailers) shows trailers' '
	git log --no-walk --pretty="%(trailers)" >actual &&
	{
		cat trailers &&
		echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:) enables no options' '
	git log --no-walk --pretty="%(trailers:)" >actual &&
	# "expect" the same as the test above
	test_cmp expect actual
'

test_expect_success '%(trailers:only) shows only "key: value" trailers' '
	git log --no-walk --pretty="%(trailers:only)" >actual &&
	{
		grep -v patch.description <trailers &&
		echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:only=yes) shows only "key: value" trailers' '
	git log --no-walk --pretty=format:"%(trailers:only=yes)" >actual &&
	grep -v patch.description <trailers >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:only=no) shows all trailers' '
	git log --no-walk --pretty=format:"%(trailers:only=no)" >actual &&
	cat trailers >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:only=no,only=true) shows only "key: value" trailers' '
	git log --no-walk --pretty=format:"%(trailers:only=yes)" >actual &&
	grep -v patch.description <trailers >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:unfold) unfolds trailers' '
	git log --no-walk --pretty="%(trailers:unfold)" >actual &&
	{
		unfold <trailers &&
		echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success ':only and :unfold work together' '
	git log --no-walk --pretty="%(trailers:only,unfold)" >actual &&
	git log --no-walk --pretty="%(trailers:unfold,only)" >reverse &&
	test_cmp actual reverse &&
	{
		grep -v patch.description <trailers | unfold &&
		echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:key=foo) shows that trailer' '
	git log --no-walk --pretty="format:%(trailers:key=Acked-by)" >actual &&
	echo "Acked-by: A U Thor <author@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:key=foo) is case insensitive' '
	git log --no-walk --pretty="format:%(trailers:key=AcKed-bY)" >actual &&
	echo "Acked-by: A U Thor <author@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:key=foo:) trailing colon also works' '
	git log --no-walk --pretty="format:%(trailers:key=Acked-by:)" >actual &&
	echo "Acked-by: A U Thor <author@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:key=foo) multiple keys' '
	git log --no-walk --pretty="format:%(trailers:key=Acked-by:,key=Signed-off-By)" >actual &&
	grep -v patch.description <trailers >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:key=nonexistent) becomes empty' '
	git log --no-walk --pretty="x%(trailers:key=Nacked-by)x" >actual &&
	echo "xx" >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:key=foo) handles multiple lines even if folded' '
	git log --no-walk --pretty="format:%(trailers:key=Signed-Off-by)" >actual &&
	grep -v patch.description <trailers | grep -v Acked-by >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:key=foo,unfold) properly unfolds' '
	git log --no-walk --pretty="format:%(trailers:key=Signed-Off-by,unfold)" >actual &&
	unfold <trailers | grep Signed-off-by >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:key=foo,only=no) also includes nontrailer lines' '
	git log --no-walk --pretty="format:%(trailers:key=Acked-by,only=no)" >actual &&
	{
		echo "Acked-by: A U Thor <author@example.com>" &&
		grep patch.description <trailers
	} >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:key) without value is error' '
	git log --no-walk --pretty="tformat:%(trailers:key)" >actual &&
	echo "%(trailers:key)" >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:keyonly) shows only keys' '
	git log --no-walk --pretty="format:%(trailers:keyonly)" >actual &&
	test_write_lines \
		"Signed-off-by" \
		"Acked-by" \
		"[ v2 updated patch description ]" \
		"Signed-off-by" >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:key=foo,keyonly) shows only key' '
	git log --no-walk --pretty="format:%(trailers:key=Acked-by,keyonly)" >actual &&
	echo "Acked-by" >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:key=foo,valueonly) shows only value' '
	git log --no-walk --pretty="format:%(trailers:key=Acked-by,valueonly)" >actual &&
	echo "A U Thor <author@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:valueonly) shows only values' '
	git log --no-walk --pretty="format:%(trailers:valueonly)" >actual &&
	test_write_lines \
		"A U Thor <author@example.com>" \
		"A U Thor <author@example.com>" \
		"[ v2 updated patch description ]" \
		"A U Thor" \
		"  <author@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success '%(trailers:key=foo,keyonly,valueonly) shows nothing' '
	git log --no-walk --pretty="format:%(trailers:key=Acked-by,keyonly,valueonly)" >actual &&
	echo >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:separator) changes separator' '
	git log --no-walk --pretty=format:"X%(trailers:separator=%x00)X" >actual &&
	(
		printf "XSigned-off-by: A U Thor <author@example.com>\0" &&
		printf "Acked-by: A U Thor <author@example.com>\0" &&
		printf "[ v2 updated patch description ]\0" &&
		printf "Signed-off-by: A U Thor\n  <author@example.com>X"
	) >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:separator=X,unfold) changes separator' '
	git log --no-walk --pretty=format:"X%(trailers:separator=%x00,unfold)X" >actual &&
	(
		printf "XSigned-off-by: A U Thor <author@example.com>\0" &&
		printf "Acked-by: A U Thor <author@example.com>\0" &&
		printf "[ v2 updated patch description ]\0" &&
		printf "Signed-off-by: A U Thor <author@example.com>X"
	) >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:key_value_separator) changes key-value separator' '
	git log --no-walk --pretty=format:"X%(trailers:key_value_separator=%x00)X" >actual &&
	(
		printf "XSigned-off-by\0A U Thor <author@example.com>\n" &&
		printf "Acked-by\0A U Thor <author@example.com>\n" &&
		printf "[ v2 updated patch description ]\n" &&
		printf "Signed-off-by\0A U Thor\n  <author@example.com>\nX"
	) >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:key_value_separator,unfold) changes key-value separator' '
	git log --no-walk --pretty=format:"X%(trailers:key_value_separator=%x00,unfold)X" >actual &&
	(
		printf "XSigned-off-by\0A U Thor <author@example.com>\n" &&
		printf "Acked-by\0A U Thor <author@example.com>\n" &&
		printf "[ v2 updated patch description ]\n" &&
		printf "Signed-off-by\0A U Thor <author@example.com>\nX"
	) >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers:separator,key_value_separator) changes both separators' '
	git log --no-walk --pretty=format:"%(trailers:separator=%x00,key_value_separator=%x00%x00,unfold)" >actual &&
	(
		printf "Signed-off-by\0\0A U Thor <author@example.com>\0" &&
		printf "Acked-by\0\0A U Thor <author@example.com>\0" &&
		printf "[ v2 updated patch description ]\0" &&
		printf "Signed-off-by\0\0A U Thor <author@example.com>"
	) >expect &&
	test_cmp expect actual
'

test_expect_success 'pretty format %(trailers) combining separator/key/keyonly/valueonly' '
	git commit --allow-empty -F - <<-\EOF &&
	Important fix

	The fix is explained here

	Closes: #1234
	EOF

	git commit --allow-empty -F - <<-\EOF &&
	Another fix

	The fix is explained here

	Closes: #567
	Closes: #890
	EOF

	git commit --allow-empty -F - <<-\EOF &&
	Does not close any tickets
	EOF

	git log --pretty="%s% (trailers:separator=%x2c%x20,key=Closes,valueonly)" HEAD~3.. >actual &&
	test_write_lines \
		"Does not close any tickets" \
		"Another fix #567, #890" \
		"Important fix #1234" >expect &&
	test_cmp expect actual &&

	git log --pretty="%s% (trailers:separator=%x2c%x20,key=Closes,keyonly)" HEAD~3.. >actual &&
	test_write_lines \
		"Does not close any tickets" \
		"Another fix Closes, Closes" \
		"Important fix Closes" >expect &&
	test_cmp expect actual
'

test_expect_success 'trailer parsing not fooled by --- line' '
	git commit --allow-empty -F - <<-\EOF &&
	this is the subject

	This is the body. The message has a "---" line which would confuse a
	message+patch parser. But here we know we have only a commit message,
	so we get it right.

	trailer: wrong
	---
	This is more body.

	trailer: right
	EOF

	{
		echo "trailer: right" &&
		echo
	} >expect &&
	git log --no-walk --format="%(trailers)" >actual &&
	test_cmp expect actual
'

test_expect_success 'set up %S tests' '
	git checkout --orphan source-a &&
	test_commit one &&
	test_commit two &&
	git checkout -b source-b HEAD^ &&
	test_commit three
'

test_expect_success 'log --format=%S paints branch names' '
	cat >expect <<-\EOF &&
	source-b
	source-a
	source-b
	EOF
	git log --format=%S source-a source-b >actual &&
	test_cmp expect actual
'

test_expect_success 'log --format=%S paints tag names' '
	git tag -m tagged source-tag &&
	cat >expect <<-\EOF &&
	source-tag
	source-a
	source-tag
	EOF
	git log --format=%S source-tag source-a >actual &&
	test_cmp expect actual
'

test_expect_success 'log --format=%S paints symmetric ranges' '
	cat >expect <<-\EOF &&
	source-b
	source-a
	EOF
	git log --format=%S source-a...source-b >actual &&
	test_cmp expect actual
'

test_expect_success '%S in git log --format works with other placeholders (part 1)' '
	git log --format="source-b %h" source-b >expect &&
	git log --format="%S %h" source-b >actual &&
	test_cmp expect actual
'

test_expect_success '%S in git log --format works with other placeholders (part 2)' '
	git log --format="%h source-b" source-b >expect &&
	git log --format="%h %S" source-b >actual &&
	test_cmp expect actual
'

test_expect_success 'setup more commits for %S with --bisect' '
	test_commit four &&
	test_commit five &&

	head1=$(git rev-parse --verify HEAD~0) &&
	head2=$(git rev-parse --verify HEAD~1) &&
	head3=$(git rev-parse --verify HEAD~2) &&
	head4=$(git rev-parse --verify HEAD~3)
'

test_expect_success '%S with --bisect labels commits with refs/bisect/bad ref' '
	git update-ref refs/bisect/bad-$head1 $head1 &&
	git update-ref refs/bisect/go $head1 &&
	git update-ref refs/bisect/bad-$head2 $head2 &&
	git update-ref refs/bisect/b $head3 &&
	git update-ref refs/bisect/bad-$head4 $head4 &&
	git update-ref refs/bisect/good-$head4 $head4 &&

	# We expect to see the range of commits betwee refs/bisect/good-$head4
	# and refs/bisect/bad-$head1. The "source" ref is the nearest bisect ref
	# from which the commit is reachable.
	cat >expect <<-EOF &&
	$head1 refs/bisect/bad-$head1
	$head2 refs/bisect/bad-$head2
	$head3 refs/bisect/bad-$head2
	EOF
	git log --bisect --format="%H %S" >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty=reference' '
	git log --pretty="tformat:%h (%s, %as)" >expect &&
	git log --pretty=reference >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty=reference with log.date is overridden by short date' '
	git log --pretty="tformat:%h (%s, %as)" >expect &&
	test_config log.date rfc &&
	git log --pretty=reference >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty=reference with explicit date overrides short date' '
	git log --date=rfc --pretty="tformat:%h (%s, %ad)" >expect &&
	git log --date=rfc --pretty=reference >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty=reference is never unabbreviated' '
	git log --pretty="tformat:%h (%s, %as)" >expect &&
	git log --no-abbrev-commit --pretty=reference >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty=reference is never decorated' '
	git log --pretty="tformat:%h (%s, %as)" >expect &&
	git log --decorate=short --pretty=reference >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty=reference does not output reflog info' '
	git log --walk-reflogs --pretty="tformat:%h (%s, %as)" >expect &&
	git log --walk-reflogs --pretty=reference >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty=reference is colored appropriately' '
	git log --color=always --pretty="tformat:%C(auto)%h (%s, %as)" >expect &&
	git log --color=always --pretty=reference >actual &&
	test_cmp expect actual
'

test_expect_success '%(describe) vs git describe' '
	git log --format="%H" | while read hash
	do
		if desc=$(git describe $hash)
		then
			: >expect-contains-good
		else
			: >expect-contains-bad
		fi &&
		echo "$hash $desc" || return 1
	done >expect &&
	test_path_exists expect-contains-good &&
	test_path_exists expect-contains-bad &&

	git log --format="%H %(describe)" >actual 2>err &&
	test_cmp expect actual &&
	test_must_be_empty err
'

test_expect_success '%(describe:match=...) vs git describe --match ...' '
	test_when_finished "git tag -d tag-match" &&
	git tag -a -m tagged tag-match &&
	git describe --match "*-match" >expect &&
	git log -1 --format="%(describe:match=*-match)" >actual &&
	test_cmp expect actual
'

test_expect_success '%(describe:exclude=...) vs git describe --exclude ...' '
	test_when_finished "git tag -d tag-exclude" &&
	git tag -a -m tagged tag-exclude &&
	git describe --exclude "*-exclude" >expect &&
	git log -1 --format="%(describe:exclude=*-exclude)" >actual &&
	test_cmp expect actual
'

test_expect_success '%(describe:tags) vs git describe --tags' '
	test_when_finished "git tag -d tagname" &&
	git tag tagname &&
	git describe --tags >expect &&
	git log -1 --format="%(describe:tags)" >actual &&
	test_cmp expect actual
'

test_expect_success '%(describe:abbrev=...) vs git describe --abbrev=...' '
	test_when_finished "git tag -d tagname" &&

	# Case 1: We have commits between HEAD and the most recent tag
	#	  reachable from it
	test_commit --no-tag file &&
	git describe --abbrev=15 >expect &&
	git log -1 --format="%(describe:abbrev=15)" >actual &&
	test_cmp expect actual &&

	# Make sure the hash used is at least 15 digits long
	sed -e "s/^.*-g\([0-9a-f]*\)$/\1/" <actual >hexpart &&
	test 16 -le $(wc -c <hexpart) &&

	# Case 2: We have a tag at HEAD, describe directly gives the
	#	  name of the tag
	git tag -a -m tagged tagname &&
	git describe --abbrev=15 >expect &&
	git log -1 --format="%(describe:abbrev=15)" >actual &&
	test_cmp expect actual &&
	test tagname = $(cat actual)
'

test_expect_success 'log --pretty with space stealing' '
	printf mm0 >expect &&
	git log -1 --pretty="format:mm%>>|(1)%x30" >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty with invalid padding format' '
	printf "%s%%<(20" "$(git rev-parse HEAD)" >expect &&
	git log -1 --pretty="format:%H%<(20" >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty with magical wrapping directives' '
	commit_id=$(git commit-tree HEAD^{tree} -m "describe me") &&
	git tag describe-me $commit_id &&
	printf "\n(tag:\ndescribe-me)%%+w(2)" >expect &&
	git log -1 --pretty="format:%w(1)%+d%+w(2)" $commit_id >actual &&
	test_cmp expect actual
'

test_expect_success SIZE_T_IS_64BIT 'log --pretty with overflowing wrapping directive' '
	printf "%%w(2147483649,1,1)0" >expect &&
	git log -1 --pretty="format:%w(2147483649,1,1)%x30" >actual &&
	test_cmp expect actual &&
	printf "%%w(1,2147483649,1)0" >expect &&
	git log -1 --pretty="format:%w(1,2147483649,1)%x30" >actual &&
	test_cmp expect actual &&
	printf "%%w(1,1,2147483649)0" >expect &&
	git log -1 --pretty="format:%w(1,1,2147483649)%x30" >actual &&
	test_cmp expect actual
'

test_expect_success SIZE_T_IS_64BIT 'log --pretty with overflowing padding directive' '
	printf "%%<(2147483649)0" >expect &&
	git log -1 --pretty="format:%<(2147483649)%x30" >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty with padding and preceding control chars' '
	printf "\20\20   0" >expect &&
	git log -1 --pretty="format:%x10%x10%>|(4)%x30" >actual &&
	test_cmp expect actual
'

test_expect_success 'log --pretty truncation with control chars' '
	test_commit "$(printf "\20\20\20\20xxxx")" file contents commit-with-control-chars &&
	printf "\20\20\20\20x.." >expect &&
	git log -1 --pretty="format:%<(3,trunc)%s" commit-with-control-chars >actual &&
	test_cmp expect actual
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT 'log --pretty with huge commit message' '
	# We only assert that this command does not crash. This needs to be
	# executed with the address sanitizer to demonstrate failure.
	git log -1 --pretty="format:%>(2147483646)%x41%41%>(2147483646)%x41" >/dev/null
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT 'set up huge commit' '
	test-tool genzeros 2147483649 | tr "\000" "1" >expect &&
	huge_commit=$(git commit-tree -F expect HEAD^{tree})
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT 'log --pretty with huge commit message' '
	git log -1 --format="%B%<(1)%x30" $huge_commit >actual &&
	echo 0 >>expect &&
	test_cmp expect actual
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT 'log --pretty with huge commit message does not cause allocation failure' '
	test_must_fail git log -1 --format="%<(1)%B" $huge_commit 2>error &&
	cat >expect <<-EOF &&
	fatal: number too large to represent as int on this platform: 2147483649
	EOF
	test_cmp expect error
'

# pretty-formats note wide char limitations, and add tests
test_expect_failure 'wide and decomposed characters column counting' '

# from t/lib-unicode-nfc-nfd.sh hex values converted to octal
	utf8_nfc=$(printf "\303\251") && # e acute combined.
	utf8_nfd=$(printf "\145\314\201") && # e with a combining acute (i.e. decomposed)
	utf8_emoji=$(printf "\360\237\221\250") &&

# replacement character when requesting a wide char fits in a single display colum.
# "half wide" alternative could be a plain ASCII dot `.`
	utf8_vert_ell=$(printf "\342\213\256") &&

# use ${xxx} here!
	nfc10="${utf8_nfc}${utf8_nfc}${utf8_nfc}${utf8_nfc}${utf8_nfc}${utf8_nfc}${utf8_nfc}${utf8_nfc}${utf8_nfc}${utf8_nfc}" &&
	nfd10="${utf8_nfd}${utf8_nfd}${utf8_nfd}${utf8_nfd}${utf8_nfd}${utf8_nfd}${utf8_nfd}${utf8_nfd}${utf8_nfd}${utf8_nfd}" &&
	emoji5="${utf8_emoji}${utf8_emoji}${utf8_emoji}${utf8_emoji}${utf8_emoji}" &&
# emoji5 uses 10 display columns

	test_commit "abcdefghij" &&
	test_commit --no-tag "${nfc10}" &&
	test_commit --no-tag "${nfd10}" &&
	test_commit --no-tag "${emoji5}" &&
	printf "${utf8_emoji}..${utf8_emoji}${utf8_vert_ell}\n${utf8_nfd}..${utf8_nfd}${utf8_nfd}\n${utf8_nfc}..${utf8_nfc}${utf8_nfc}\na..ij\n" >expected &&
	git log --format="%<(5,mtrunc)%s" -4 >actual &&
	test_cmp expected actual
'

test_done
