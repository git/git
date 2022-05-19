#!/bin/sh

# Copyright (c) 2009 Jens Lehmann
# Copyright (c) 2011 Alexey Shumkin (+ non-UTF-8 cummit encoding tests)

test_description='but rev-list --pretty=format test'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_tick
# Tested non-UTF-8 encoding
test_encoding="ISO8859-1"

# String "added" in German
# (translated with Google Translate),
# encoded in UTF-8, used as a cummit log message below.
added_utf8_part=$(printf "\303\274")
added_utf8_part_iso88591=$(echo "$added_utf8_part" | iconv -f utf-8 -t $test_encoding)
added=$(printf "added (hinzugef${added_utf8_part}gt) foo")
added_iso88591=$(echo "$added" | iconv -f utf-8 -t $test_encoding)
# same but "changed"
changed_utf8_part=$(printf "\303\244")
changed_utf8_part_iso88591=$(echo "$changed_utf8_part" | iconv -f utf-8 -t $test_encoding)
changed=$(printf "changed (ge${changed_utf8_part}ndert) foo")
changed_iso88591=$(echo "$changed" | iconv -f utf-8 -t $test_encoding)

# Count of char to truncate
# Number is chosen so, that non-ACSII characters
# (see $added_utf8_part and $changed_utf8_part)
# fall into truncated parts of appropriate words both from left and right
truncate_count=20

test_expect_success 'setup' '
	: >foo &&
	but add foo &&
	but config i18n.cummitEncoding $test_encoding &&
	echo "$added_iso88591" | but cummit -F - &&
	head1=$(but rev-parse --verify HEAD) &&
	head1_short=$(but rev-parse --verify --short $head1) &&
	head1_short4=$(but rev-parse --verify --short=4 $head1) &&
	tree1=$(but rev-parse --verify HEAD:) &&
	tree1_short=$(but rev-parse --verify --short $tree1) &&
	echo "$changed" > foo &&
	echo "$changed_iso88591" | but cummit -a -F - &&
	head2=$(but rev-parse --verify HEAD) &&
	head2_short=$(but rev-parse --verify --short $head2) &&
	head2_short4=$(but rev-parse --verify --short=4 $head2) &&
	tree2=$(but rev-parse --verify HEAD:) &&
	tree2_short=$(but rev-parse --verify --short $tree2) &&
	but config --unset i18n.cummitEncoding
'

# usage: test_format [argument...] name format_string [failure] <expected_output
test_format () {
	local args=
	while true
	do
		case "$1" in
		--*)
			args="$args $1"
			shift;;
		*)
			break;;
		esac
	done
	cat >expect.$1
	test_expect_${3:-success} "format $1" "
		but rev-list $args --pretty=format:'$2' main >output.$1 &&
		test_cmp expect.$1 output.$1
	"
}

# usage: test_pretty [argument...] name format_name [failure] <expected_output
test_pretty () {
	local args=
	while true
	do
		case "$1" in
		--*)
			args="$args $1"
			shift;;
		*)
			break;;
		esac
	done
	cat >expect.$1
	test_expect_${3:-success} "pretty $1 (without --no-commit-header)" "
		but rev-list $args --pretty='$2' main >output.$1 &&
		test_cmp expect.$1 output.$1
	"
	test_expect_${3:-success} "pretty $1 (with --no-commit-header)" "
		but rev-list $args --no-commit-header --pretty='$2' main >output.$1 &&
		test_cmp expect.$1 output.$1
	"
}

# Feed to --format to provide predictable colored sequences.
BASIC_COLOR='%Credfoo%Creset'
COLOR='%C(red)foo%C(reset)'
AUTO_COLOR='%C(auto,red)foo%C(auto,reset)'
ALWAYS_COLOR='%C(always,red)foo%C(always,reset)'
has_color () {
	test_decode_color <"$1" >decoded &&
	echo "<RED>foo<RESET>" >expect &&
	test_cmp expect decoded
}

has_no_color () {
	echo foo >expect &&
	test_cmp expect "$1"
}

test_format percent %%h <<EOF
cummit $head2
%h
cummit $head1
%h
EOF

test_format hash %H%n%h <<EOF
cummit $head2
$head2
$head2_short
cummit $head1
$head1
$head1_short
EOF

test_format --no-commit-header hash-no-header %H%n%h <<EOF
$head2
$head2_short
$head1
$head1_short
EOF

test_format --abbrev-cummit --abbrev=0 --no-commit-header hash-no-header-abbrev %H%n%h <<EOF
$head2
$head2_short4
$head1
$head1_short4
EOF

test_format tree %T%n%t <<EOF
cummit $head2
$tree2
$tree2_short
cummit $head1
$tree1
$tree1_short
EOF

test_format parents %P%n%p <<EOF
cummit $head2
$head1
$head1_short
cummit $head1


EOF

# we don't test relative here
test_format author %an%n%ae%n%al%n%ad%n%aD%n%at <<EOF
cummit $head2
$BUT_AUTHOR_NAME
$BUT_AUTHOR_EMAIL
$TEST_AUTHOR_LOCALNAME
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
cummit $head1
$BUT_AUTHOR_NAME
$BUT_AUTHOR_EMAIL
$TEST_AUTHOR_LOCALNAME
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
EOF

test_format cummitter %cn%n%ce%n%cl%n%cd%n%cD%n%ct <<EOF
cummit $head2
$BUT_CUMMITTER_NAME
$BUT_CUMMITTER_EMAIL
$TEST_CUMMITTER_LOCALNAME
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
cummit $head1
$BUT_CUMMITTER_NAME
$BUT_CUMMITTER_EMAIL
$TEST_CUMMITTER_LOCALNAME
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
EOF

test_format encoding %e <<EOF
cummit $head2
$test_encoding
cummit $head1
$test_encoding
EOF

test_format subject %s <<EOF
cummit $head2
$changed
cummit $head1
$added
EOF

test_format subject-truncated "%<($truncate_count,trunc)%s" <<EOF
cummit $head2
changed (ge${changed_utf8_part}ndert)..
cummit $head1
added (hinzugef${added_utf8_part}gt..
EOF

test_format body %b <<EOF
cummit $head2
cummit $head1
EOF

test_format raw-body %B <<EOF
cummit $head2
$changed

cummit $head1
$added

EOF

test_format --no-commit-header raw-body-no-header %B <<EOF
$changed

$added

EOF

test_pretty oneline oneline <<EOF
$head2 $changed
$head1 $added
EOF

test_pretty short short <<EOF
cummit $head2
Author: $BUT_AUTHOR_NAME <$BUT_AUTHOR_EMAIL>

    $changed

cummit $head1
Author: $BUT_AUTHOR_NAME <$BUT_AUTHOR_EMAIL>

    $added

EOF

test_expect_success 'basic colors' '
	cat >expect <<-EOF &&
	cummit $head2
	<RED>foo<GREEN>bar<BLUE>baz<RESET>xyzzy
	EOF
	format="%Credfoo%Cgreenbar%Cbluebaz%Cresetxyzzy" &&
	but rev-list --color --format="$format" -1 main >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success '%S is not a placeholder for rev-list yet' '
	but rev-list --format="%S" -1 main | grep "%S"
'

test_expect_success 'advanced colors' '
	cat >expect <<-EOF &&
	cummit $head2
	<BOLD;RED;BYELLOW>foo<RESET>
	EOF
	format="%C(red yellow bold)foo%C(reset)" &&
	but rev-list --color --format="$format" -1 main >actual.raw &&
	test_decode_color <actual.raw >actual &&
	test_cmp expect actual
'

for spec in \
	"%Cred:$BASIC_COLOR" \
	"%C(...):$COLOR" \
	"%C(auto,...):$AUTO_COLOR"
do
	desc=${spec%%:*}
	color=${spec#*:}
	test_expect_success "$desc does not enable color by default" '
		but log --format=$color -1 >actual &&
		has_no_color actual
	'

	test_expect_success "$desc enables colors for color.diff" '
		but -c color.diff=always log --format=$color -1 >actual &&
		has_color actual
	'

	test_expect_success "$desc enables colors for color.ui" '
		but -c color.ui=always log --format=$color -1 >actual &&
		has_color actual
	'

	test_expect_success "$desc respects --color" '
		but log --format=$color -1 --color >actual &&
		has_color actual
	'

	test_expect_success "$desc respects --no-color" '
		but -c color.ui=always log --format=$color -1 --no-color >actual &&
		has_no_color actual
	'

	test_expect_success TTY "$desc respects --color=auto (stdout is tty)" '
		test_terminal but log --format=$color -1 --color=auto >actual &&
		has_color actual
	'

	test_expect_success "$desc respects --color=auto (stdout not tty)" '
		(
			TERM=vt100 && export TERM &&
			but log --format=$color -1 --color=auto >actual &&
			has_no_color actual
		)
	'
done

test_expect_success '%C(always,...) enables color even without tty' '
	but log --format=$ALWAYS_COLOR -1 >actual &&
	has_color actual
'

test_expect_success '%C(auto) respects --color' '
	but log --color --format="%C(auto)%H" -1 >actual.raw &&
	test_decode_color <actual.raw >actual &&
	echo "<YELLOW>$(but rev-parse HEAD)<RESET>" >expect &&
	test_cmp expect actual
'

test_expect_success '%C(auto) respects --no-color' '
	but log --no-color --format="%C(auto)%H" -1 >actual &&
	but rev-parse HEAD >expect &&
	test_cmp expect actual
'

test_expect_success 'rev-list %C(auto,...) respects --color' '
	but rev-list --color --format="%C(auto,green)foo%C(auto,reset)" \
		-1 HEAD >actual.raw &&
	test_decode_color <actual.raw >actual &&
	cat >expect <<-EOF &&
	cummit $(but rev-parse HEAD)
	<GREEN>foo<RESET>
	EOF
	test_cmp expect actual
'

iconv -f utf-8 -t $test_encoding > cummit-msg <<EOF
Test printing of complex bodies

This cummit message is much longer than the others,
and it will be encoded in $test_encoding. We should therefore
include an ISO8859 character: ¡bueno!
EOF

test_expect_success 'setup complex body' '
	but config i18n.cummitencoding $test_encoding &&
	echo change2 >foo && but cummit -a -F cummit-msg &&
	head3=$(but rev-parse --verify HEAD) &&
	head3_short=$(but rev-parse --short $head3)
'

test_format complex-encoding %e <<EOF
cummit $head3
$test_encoding
cummit $head2
$test_encoding
cummit $head1
$test_encoding
EOF

test_format complex-subject %s <<EOF
cummit $head3
Test printing of complex bodies
cummit $head2
$changed_iso88591
cummit $head1
$added_iso88591
EOF

test_format complex-subject-trunc "%<($truncate_count,trunc)%s" <<EOF
cummit $head3
Test printing of c..
cummit $head2
changed (ge${changed_utf8_part_iso88591}ndert)..
cummit $head1
added (hinzugef${added_utf8_part_iso88591}gt..
EOF

test_format complex-subject-mtrunc "%<($truncate_count,mtrunc)%s" <<EOF
cummit $head3
Test prin..ex bodies
cummit $head2
changed (..dert) foo
cummit $head1
added (hi..f${added_utf8_part_iso88591}gt) foo
EOF

test_format complex-subject-ltrunc "%<($truncate_count,ltrunc)%s" <<EOF
cummit $head3
.. of complex bodies
cummit $head2
..ged (ge${changed_utf8_part_iso88591}ndert) foo
cummit $head1
.. (hinzugef${added_utf8_part_iso88591}gt) foo
EOF

test_expect_success 'setup expected messages (for test %b)' '
	cat <<-EOF >expected.utf-8 &&
	cummit $head3
	This cummit message is much longer than the others,
	and it will be encoded in $test_encoding. We should therefore
	include an ISO8859 character: ¡bueno!

	cummit $head2
	cummit $head1
	EOF
	iconv -f utf-8 -t $test_encoding expected.utf-8 >expected.ISO8859-1
'

test_format complex-body %b <expected.ISO8859-1

# Git uses i18n.cummitEncoding if no i18n.logOutputEncoding set
# so unset i18n.cummitEncoding to test encoding conversion
but config --unset i18n.cummitEncoding

test_format complex-subject-cummitencoding-unset %s <<EOF
cummit $head3
Test printing of complex bodies
cummit $head2
$changed
cummit $head1
$added
EOF

test_format complex-subject-cummitencoding-unset-trunc "%<($truncate_count,trunc)%s" <<EOF
cummit $head3
Test printing of c..
cummit $head2
changed (ge${changed_utf8_part}ndert)..
cummit $head1
added (hinzugef${added_utf8_part}gt..
EOF

test_format complex-subject-cummitencoding-unset-mtrunc "%<($truncate_count,mtrunc)%s" <<EOF
cummit $head3
Test prin..ex bodies
cummit $head2
changed (..dert) foo
cummit $head1
added (hi..f${added_utf8_part}gt) foo
EOF

test_format complex-subject-cummitencoding-unset-ltrunc "%<($truncate_count,ltrunc)%s" <<EOF
cummit $head3
.. of complex bodies
cummit $head2
..ged (ge${changed_utf8_part}ndert) foo
cummit $head1
.. (hinzugef${added_utf8_part}gt) foo
EOF

test_format complex-body-cummitencoding-unset %b <expected.utf-8

test_expect_success '%x00 shows NUL' '
	echo  >expect cummit $head3 &&
	echo >>expect fooQbar &&
	but rev-list -1 --format=foo%x00bar HEAD >actual.nul &&
	nul_to_q <actual.nul >actual &&
	test_cmp expect actual
'

test_expect_success '%ad respects --date=' '
	echo 2005-04-07 >expect.ad-short &&
	but log -1 --date=short --pretty=tformat:%ad >output.ad-short main &&
	test_cmp expect.ad-short output.ad-short
'

test_expect_success 'empty email' '
	test_tick &&
	C=$(BUT_AUTHOR_EMAIL= but cummit-tree HEAD^{tree} </dev/null) &&
	A=$(but show --pretty=format:%an,%ae,%ad%n -s $C) &&
	verbose test "$A" = "$BUT_AUTHOR_NAME,,Thu Apr 7 15:14:13 2005 -0700"
'

test_expect_success 'del LF before empty (1)' '
	but show -s --pretty=format:"%s%n%-b%nThanks%n" HEAD^^ >actual &&
	test_line_count = 2 actual
'

test_expect_success 'del LF before empty (2)' '
	but show -s --pretty=format:"%s%n%-b%nThanks%n" HEAD >actual &&
	test_line_count = 6 actual &&
	grep "^$" actual
'

test_expect_success 'add LF before non-empty (1)' '
	but show -s --pretty=format:"%s%+b%nThanks%n" HEAD^^ >actual &&
	test_line_count = 2 actual
'

test_expect_success 'add LF before non-empty (2)' '
	but show -s --pretty=format:"%s%+b%nThanks%n" HEAD >actual &&
	test_line_count = 6 actual &&
	grep "^$" actual
'

test_expect_success 'add SP before non-empty (1)' '
	but show -s --pretty=format:"%s% bThanks" HEAD^^ >actual &&
	test $(wc -w <actual) = 3
'

test_expect_success 'add SP before non-empty (2)' '
	but show -s --pretty=format:"%s% sThanks" HEAD^^ >actual &&
	test $(wc -w <actual) = 6
'

test_expect_success '--abbrev' '
	echo SHORT SHORT SHORT >expect2 &&
	echo LONG LONG LONG >expect3 &&
	but log -1 --format="%h %h %h" HEAD >actual1 &&
	but log -1 --abbrev=5 --format="%h %h %h" HEAD >actual2 &&
	but log -1 --abbrev=5 --format="%H %H %H" HEAD >actual3 &&
	sed -e "s/$OID_REGEX/LONG/g" -e "s/$_x05/SHORT/g" <actual2 >fuzzy2 &&
	sed -e "s/$OID_REGEX/LONG/g" -e "s/$_x05/SHORT/g" <actual3 >fuzzy3 &&
	test_cmp expect2 fuzzy2 &&
	test_cmp expect3 fuzzy3 &&
	! test_cmp actual1 actual2
'

test_expect_success '%H is not affected by --abbrev-cummit' '
	expected=$(($(test_oid hexsz) + 1)) &&
	but log -1 --format=%H --abbrev-cummit --abbrev=20 HEAD >actual &&
	len=$(wc -c <actual) &&
	test $len = $expected
'

test_expect_success '%h is not affected by --abbrev-cummit' '
	but log -1 --format=%h --abbrev-cummit --abbrev=20 HEAD >actual &&
	len=$(wc -c <actual) &&
	test $len = 21
'

test_expect_success '"%h %gD: %gs" is same as but-reflog' '
	but reflog >expect &&
	but log -g --format="%h %gD: %gs" >actual &&
	test_cmp expect actual
'

test_expect_success '"%h %gD: %gs" is same as but-reflog (with date)' '
	but reflog --date=raw >expect &&
	but log -g --format="%h %gD: %gs" --date=raw >actual &&
	test_cmp expect actual
'

test_expect_success '"%h %gD: %gs" is same as but-reflog (with --abbrev)' '
	but reflog --abbrev=13 --date=raw >expect &&
	but log -g --abbrev=13 --format="%h %gD: %gs" --date=raw >actual &&
	test_cmp expect actual
'

test_expect_success '%gd shortens ref name' '
	echo "main@{0}" >expect.gd-short &&
	but log -g -1 --format=%gd refs/heads/main >actual.gd-short &&
	test_cmp expect.gd-short actual.gd-short
'

test_expect_success 'reflog identity' '
	echo "$BUT_CUMMITTER_NAME:$BUT_CUMMITTER_EMAIL" >expect &&
	but log -g -1 --format="%gn:%ge" >actual &&
	test_cmp expect actual
'

test_expect_success 'oneline with empty message' '
	but cummit --allow-empty --cleanup=verbatim -m "$LF" &&
	but cummit --allow-empty --allow-empty-message &&
	but rev-list --oneline HEAD >test.txt &&
	test_line_count = 5 test.txt &&
	but rev-list --oneline --graph HEAD >testg.txt &&
	test_line_count = 5 testg.txt
'

test_expect_success 'single-character name is parsed correctly' '
	but cummit --author="a <a@example.com>" --allow-empty -m foo &&
	echo "a <a@example.com>" >expect &&
	but log -1 --format="%an <%ae>" >actual &&
	test_cmp expect actual
'

test_expect_success 'unused %G placeholders are passed through' '
	echo "%GX %G" >expect &&
	but log -1 --format="%GX %G" >actual &&
	test_cmp expect actual
'

test_done
