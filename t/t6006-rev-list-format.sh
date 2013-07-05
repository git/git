#!/bin/sh

# Copyright (c) 2009 Jens Lehmann
# Copyright (c) 2011 Alexey Shumkin (+ non-UTF-8 commit encoding tests)

test_description='git rev-list --pretty=format test'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_tick
# String "added" in German
# (translated with Google Translate),
# encoded in UTF-8, used as a commit log message below.
added=$(printf "added (hinzugef\303\274gt) foo")
added_iso88591=$(echo "$added" | iconv -f utf-8 -t iso8859-1)
# same but "changed"
changed=$(printf "changed (ge\303\244ndert) foo")
changed_iso88591=$(echo "$changed" | iconv -f utf-8 -t iso8859-1)

test_expect_success 'setup' '
	: >foo &&
	git add foo &&
	git config i18n.commitEncoding iso8859-1 &&
	git commit -m "$added_iso88591" &&
	head1=$(git rev-parse --verify HEAD) &&
	head1_short=$(git rev-parse --verify --short $head1) &&
	tree1=$(git rev-parse --verify HEAD:) &&
	tree1_short=$(git rev-parse --verify --short $tree1) &&
	echo "$changed" > foo &&
	git commit -a -m "$changed_iso88591" &&
	head2=$(git rev-parse --verify HEAD) &&
	head2_short=$(git rev-parse --verify --short $head2) &&
	tree2=$(git rev-parse --verify HEAD:) &&
	tree2_short=$(git rev-parse --verify --short $tree2)
	git config --unset i18n.commitEncoding
'

# usage: test_format name format_string [failure] <expected_output
test_format () {
	cat >expect.$1
	test_expect_${3:-success} "format $1" "
		git rev-list --pretty=format:'$2' master >output.$1 &&
		test_cmp expect.$1 output.$1
	"
}

# Feed to --format to provide predictable colored sequences.
AUTO_COLOR='%C(auto,red)foo%C(auto,reset)'
has_color () {
	printf '\033[31mfoo\033[m\n' >expect &&
	test_cmp expect "$1"
}

has_no_color () {
	echo foo >expect &&
	test_cmp expect "$1"
}

test_format percent %%h <<EOF
commit $head2
%h
commit $head1
%h
EOF

test_format hash %H%n%h <<EOF
commit $head2
$head2
$head2_short
commit $head1
$head1
$head1_short
EOF

test_format tree %T%n%t <<EOF
commit $head2
$tree2
$tree2_short
commit $head1
$tree1
$tree1_short
EOF

test_format parents %P%n%p <<EOF
commit $head2
$head1
$head1_short
commit $head1


EOF

# we don't test relative here
test_format author %an%n%ae%n%ad%n%aD%n%at <<EOF
commit $head2
A U Thor
author@example.com
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
commit $head1
A U Thor
author@example.com
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
EOF

test_format committer %cn%n%ce%n%cd%n%cD%n%ct <<EOF
commit $head2
C O Mitter
committer@example.com
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
commit $head1
C O Mitter
committer@example.com
Thu Apr 7 15:13:13 2005 -0700
Thu, 7 Apr 2005 15:13:13 -0700
1112911993
EOF

test_format encoding %e <<EOF
commit $head2
iso8859-1
commit $head1
iso8859-1
EOF

test_format subject %s <<EOF
commit $head2
$changed
commit $head1
$added
EOF

test_format body %b <<EOF
commit $head2
commit $head1
EOF

test_format raw-body %B <<EOF
commit $head2
$changed

commit $head1
$added

EOF

test_format colors %Credfoo%Cgreenbar%Cbluebaz%Cresetxyzzy <<EOF
commit $head2
[31mfoo[32mbar[34mbaz[mxyzzy
commit $head1
[31mfoo[32mbar[34mbaz[mxyzzy
EOF

test_format advanced-colors '%C(red yellow bold)foo%C(reset)' <<EOF
commit $head2
[1;31;43mfoo[m
commit $head1
[1;31;43mfoo[m
EOF

test_expect_success '%C(auto) does not enable color by default' '
	git log --format=$AUTO_COLOR -1 >actual &&
	has_no_color actual
'

test_expect_success '%C(auto) enables colors for color.diff' '
	git -c color.diff=always log --format=$AUTO_COLOR -1 >actual &&
	has_color actual
'

test_expect_success '%C(auto) enables colors for color.ui' '
	git -c color.ui=always log --format=$AUTO_COLOR -1 >actual &&
	has_color actual
'

test_expect_success '%C(auto) respects --color' '
	git log --format=$AUTO_COLOR -1 --color >actual &&
	has_color actual
'

test_expect_success '%C(auto) respects --no-color' '
	git -c color.ui=always log --format=$AUTO_COLOR -1 --no-color >actual &&
	has_no_color actual
'

test_expect_success TTY '%C(auto) respects --color=auto (stdout is tty)' '
	(
		TERM=vt100 && export TERM &&
		test_terminal \
			git log --format=$AUTO_COLOR -1 --color=auto >actual &&
		has_color actual
	)
'

test_expect_success '%C(auto) respects --color=auto (stdout not tty)' '
	(
		TERM=vt100 && export TERM &&
		git log --format=$AUTO_COLOR -1 --color=auto >actual &&
		has_no_color actual
	)
'

iconv -f utf-8 -t iso8859-1 > commit-msg <<EOF
Test printing of complex bodies

This commit message is much longer than the others,
and it will be encoded in iso8859-1. We should therefore
include an iso8859 character: ¡bueno!
EOF

test_expect_success 'setup complex body' '
	git config i18n.commitencoding iso8859-1 &&
	echo change2 >foo && git commit -a -F commit-msg &&
	head3=$(git rev-parse --verify HEAD) &&
	head3_short=$(git rev-parse --short $head3)
'

test_format complex-encoding %e <<EOF
commit $head3
iso8859-1
commit $head2
iso8859-1
commit $head1
iso8859-1
EOF

test_format complex-subject %s <<EOF
commit $head3
Test printing of complex bodies
commit $head2
$changed_iso88591
commit $head1
$added_iso88591
EOF

test_expect_success 'prepare expected messages (for test %b)' '
	cat <<-EOF >expected.utf-8 &&
	commit $head3
	This commit message is much longer than the others,
	and it will be encoded in iso8859-1. We should therefore
	include an iso8859 character: ¡bueno!

	commit $head2
	commit $head1
	EOF
	iconv -f utf-8 -t iso8859-1 expected.utf-8 >expected.iso8859-1
'

test_format complex-body %b <expected.iso8859-1

# Git uses i18n.commitEncoding if no i18n.logOutputEncoding set
# so unset i18n.commitEncoding to test encoding conversion
git config --unset i18n.commitEncoding

test_format complex-subject-commitencoding-unset %s <<EOF
commit $head3
Test printing of complex bodies
commit $head2
$changed
commit $head1
$added
EOF

test_format complex-body-commitencoding-unset %b <expected.utf-8

test_expect_success '%x00 shows NUL' '
	echo  >expect commit $head3 &&
	echo >>expect fooQbar &&
	git rev-list -1 --format=foo%x00bar HEAD >actual.nul &&
	nul_to_q <actual.nul >actual &&
	test_cmp expect actual
'

test_expect_success '%ad respects --date=' '
	echo 2005-04-07 >expect.ad-short &&
	git log -1 --date=short --pretty=tformat:%ad >output.ad-short master &&
	test_cmp expect.ad-short output.ad-short
'

test_expect_success 'empty email' '
	test_tick &&
	C=$(GIT_AUTHOR_EMAIL= git commit-tree HEAD^{tree} </dev/null) &&
	A=$(git show --pretty=format:%an,%ae,%ad%n -s $C) &&
	test "$A" = "A U Thor,,Thu Apr 7 15:14:13 2005 -0700" || {
		echo "Eh? $A" >failure
		false
	}
'

test_expect_success 'del LF before empty (1)' '
	git show -s --pretty=format:"%s%n%-b%nThanks%n" HEAD^^ >actual &&
	test_line_count = 2 actual
'

test_expect_success 'del LF before empty (2)' '
	git show -s --pretty=format:"%s%n%-b%nThanks%n" HEAD >actual &&
	test_line_count = 6 actual &&
	grep "^$" actual
'

test_expect_success 'add LF before non-empty (1)' '
	git show -s --pretty=format:"%s%+b%nThanks%n" HEAD^^ >actual &&
	test_line_count = 2 actual
'

test_expect_success 'add LF before non-empty (2)' '
	git show -s --pretty=format:"%s%+b%nThanks%n" HEAD >actual &&
	test_line_count = 6 actual &&
	grep "^$" actual
'

test_expect_success 'add SP before non-empty (1)' '
	git show -s --pretty=format:"%s% bThanks" HEAD^^ >actual &&
	test $(wc -w <actual) = 3
'

test_expect_success 'add SP before non-empty (2)' '
	git show -s --pretty=format:"%s% sThanks" HEAD^^ >actual &&
	test $(wc -w <actual) = 6
'

test_expect_success '--abbrev' '
	echo SHORT SHORT SHORT >expect2 &&
	echo LONG LONG LONG >expect3 &&
	git log -1 --format="%h %h %h" HEAD >actual1 &&
	git log -1 --abbrev=5 --format="%h %h %h" HEAD >actual2 &&
	git log -1 --abbrev=5 --format="%H %H %H" HEAD >actual3 &&
	sed -e "s/$_x40/LONG/g" -e "s/$_x05/SHORT/g" <actual2 >fuzzy2 &&
	sed -e "s/$_x40/LONG/g" -e "s/$_x05/SHORT/g" <actual3 >fuzzy3 &&
	test_cmp expect2 fuzzy2 &&
	test_cmp expect3 fuzzy3 &&
	! test_cmp actual1 actual2
'

test_expect_success '%H is not affected by --abbrev-commit' '
	git log -1 --format=%H --abbrev-commit --abbrev=20 HEAD >actual &&
	len=$(wc -c <actual) &&
	test $len = 41
'

test_expect_success '%h is not affected by --abbrev-commit' '
	git log -1 --format=%h --abbrev-commit --abbrev=20 HEAD >actual &&
	len=$(wc -c <actual) &&
	test $len = 21
'

test_expect_success '"%h %gD: %gs" is same as git-reflog' '
	git reflog >expect &&
	git log -g --format="%h %gD: %gs" >actual &&
	test_cmp expect actual
'

test_expect_success '"%h %gD: %gs" is same as git-reflog (with date)' '
	git reflog --date=raw >expect &&
	git log -g --format="%h %gD: %gs" --date=raw >actual &&
	test_cmp expect actual
'

test_expect_success '"%h %gD: %gs" is same as git-reflog (with --abbrev)' '
	git reflog --abbrev=13 --date=raw >expect &&
	git log -g --abbrev=13 --format="%h %gD: %gs" --date=raw >actual &&
	test_cmp expect actual
'

test_expect_success '%gd shortens ref name' '
	echo "master@{0}" >expect.gd-short &&
	git log -g -1 --format=%gd refs/heads/master >actual.gd-short &&
	test_cmp expect.gd-short actual.gd-short
'

test_expect_success 'reflog identity' '
	echo "C O Mitter:committer@example.com" >expect &&
	git log -g -1 --format="%gn:%ge" >actual &&
	test_cmp expect actual
'

test_expect_success 'oneline with empty message' '
	git commit -m "dummy" --allow-empty &&
	git commit -m "dummy" --allow-empty &&
	git filter-branch --msg-filter "sed -e s/dummy//" HEAD^^.. &&
	git rev-list --oneline HEAD >test.txt &&
	test_line_count = 5 test.txt &&
	git rev-list --oneline --graph HEAD >testg.txt &&
	test_line_count = 5 testg.txt
'

test_expect_success 'single-character name is parsed correctly' '
	git commit --author="a <a@example.com>" --allow-empty -m foo &&
	echo "a <a@example.com>" >expect &&
	git log -1 --format="%an <%ae>" >actual &&
	test_cmp expect actual
'

test_done
