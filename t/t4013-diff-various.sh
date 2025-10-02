#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='Various diff formatting options'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-diff.sh

if ! test_have_prereq PERL_TEST_HELPERS
then
	skip_all='skipping diff various tests; Perl not available'
	test_done
fi

test_expect_success setup '

	GIT_AUTHOR_DATE="2006-06-26 00:00:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:00:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	mkdir dir &&
	mkdir dir2 &&
	test_write_lines 1 2 3 >file0 &&
	test_write_lines A B >dir/sub &&
	cat file0 >file2 &&
	git add file0 file2 dir/sub &&
	git commit -m Initial &&

	git branch initial &&
	git branch side &&

	GIT_AUTHOR_DATE="2006-06-26 00:01:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:01:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	test_write_lines 4 5 6 >>file0 &&
	test_write_lines C D >>dir/sub &&
	rm -f file2 &&
	git update-index --remove file0 file2 dir/sub &&
	git commit -m "Second${LF}${LF}This is the second commit." &&

	GIT_AUTHOR_DATE="2006-06-26 00:02:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:02:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	test_write_lines A B C >file1 &&
	git add file1 &&
	test_write_lines E F >>dir/sub &&
	git update-index dir/sub &&
	git commit -m Third &&

	GIT_AUTHOR_DATE="2006-06-26 00:03:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:03:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	git checkout side &&
	test_write_lines A B C >>file0 &&
	test_write_lines 1 2 >>dir/sub &&
	cat dir/sub >file3 &&
	git add file3 &&
	git update-index file0 dir/sub &&
	git commit -m Side &&

	GIT_AUTHOR_DATE="2006-06-26 00:04:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:04:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	git checkout main &&
	git pull -s ours --no-rebase . side &&

	GIT_AUTHOR_DATE="2006-06-26 00:05:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:05:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&

	test_write_lines A B C >>file0 &&
	test_write_lines 1 2 >>dir/sub &&
	git update-index file0 dir/sub &&

	mkdir dir3 &&
	cp dir/sub dir3/sub &&
	test-tool chmtime +1 dir3/sub &&

	git config log.showroot false &&
	git commit --amend &&

	GIT_AUTHOR_DATE="2006-06-26 00:06:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:06:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&
	git checkout -b rearrange initial &&
	test_write_lines B A >dir/sub &&
	git add dir/sub &&
	git commit -m "Rearranged lines in dir/sub" &&
	git checkout main &&

	GIT_AUTHOR_DATE="2006-06-26 00:06:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:06:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&
	git checkout -b mode initial &&
	git update-index --chmod=+x file0 &&
	git commit -m "update mode" &&
	git checkout -f main &&

	GIT_AUTHOR_DATE="2006-06-26 00:06:00 +0000" &&
	GIT_COMMITTER_DATE="2006-06-26 00:06:00 +0000" &&
	export GIT_AUTHOR_DATE GIT_COMMITTER_DATE &&
	git checkout -b note initial &&
	git update-index --chmod=+x file2 &&
	git commit -m "update mode (file2)" &&
	git notes add -m "note" &&
	git checkout -f main &&

	# Same merge as main, but with parents reversed. Hide it in a
	# pseudo-ref to avoid impacting tests with --all.
	commit=$(echo reverse |
		 git commit-tree -p main^2 -p main^1 main^{tree}) &&
	git update-ref REVERSE $commit &&

	git config diff.renames false &&

	git show-branch
'

: <<\EOF
! [initial] Initial
 * [main] Merge branch 'side'
  ! [rearrange] Rearranged lines in dir/sub
   ! [side] Side
----
  +  [rearrange] Rearranged lines in dir/sub
 -   [main] Merge branch 'side'
 * + [side] Side
 *   [main^] Third
 *   [main~2] Second
+*++ [initial] Initial
EOF

process_diffs () {
	perl -e '
		my $oid_length = length($ARGV[0]);
		my $x40 = "[0-9a-f]{40}";
		my $xab = "[0-9a-f]{4,16}";
		my $orx = "[0-9a-f]" x $oid_length;

		sub munge_oid {
			my ($oid) = @_;
			my $x;

			return "" unless length $oid;

			if ($oid =~ /^(100644|100755|120000)$/) {
				return $oid;
			}

			if ($oid =~ /^0*$/) {
				$x = "0";
			} else {
				$x = "f";
			}

			if (length($oid) == 40) {
				return $x x $oid_length;
			} else {
				return $x x length($oid);
			}
		}

		while (<STDIN>) {
			s/($orx)/munge_oid($1)/ge;
			s/From ($x40)( |\))/"From " . munge_oid($1) . $2/ge;
			s/commit ($x40)($| \(from )($x40?)/"commit " .  munge_oid($1) . $2 . munge_oid($3)/ge;
			s/\b($x40)( |\.\.|$)/munge_oid($1) . $2/ge;
			s/^($x40)($| )/munge_oid($1) . $2/e;
			s/($xab)(\.\.|,| |\.\.\.|$)/munge_oid($1) . $2/ge;
			print;
		}
	' "$ZERO_OID" <"$1"
}

V=$(git version | sed -e 's/^git version //' -e 's/\./\\./g')
while read magic cmd
do
	case "$magic" in
	'' | '#'*)
		continue ;;
	:noellipses)
		magic=noellipses
		label="$magic-$cmd"
		;;
	:*)
		BUG "unknown magic $magic"
		;;
	*)
		cmd="$magic $cmd"
		magic=
		label="$cmd"
		;;
	esac

	test=$(echo "$label" | sed -e 's|[/ ][/ ]*|_|g')
	pfx=$(printf "%04d" $test_count)
	expect="$TEST_DIRECTORY/t4013/diff.$test"
	actual="$pfx-diff.$test"

	case "$cmd" in
	whatchanged | whatchanged" "*)
		prereq=!WITH_BREAKING_CHANGES
		;;
	*)
		prereq=;;
	esac

	test_expect_success $prereq "git $cmd # magic is ${magic:-(not used)}" '
		{
			echo "$ git $cmd"

			case "$cmd" in
			whatchanged | whatchanged" "*)
				run="whatchanged --i-still-use-this"
				run="$run ${cmd#whatchanged}" ;;
			*)
				run=$cmd ;;
			esac &&
			case "$magic" in
			"")
				GIT_PRINT_SHA1_ELLIPSIS=yes git $run ;;
			noellipses)
				git $run ;;
			esac |
			sed -e "s/^\\(-*\\)$V\\(-*\\)\$/\\1g-i-t--v-e-r-s-i-o-n\2/" \
			    -e "s/^\\(.*mixed; boundary=\"-*\\)$V\\(-*\\)\"\$/\\1g-i-t--v-e-r-s-i-o-n\2\"/"
			echo "\$"
		} >"$actual" &&
		if test -f "$expect"
		then
			process_diffs "$actual" >actual &&
			process_diffs "$expect" >expect &&
			case $cmd in
			*format-patch* | *-stat*)
				test_cmp expect actual;;
			*)
				test_cmp expect actual;;
			esac &&
			rm -f "$actual" actual expect
		else
			# this is to help developing new tests.
			cp "$actual" "$expect"
			false
		fi
	'
done <<\EOF
diff-tree initial
diff-tree -r initial
diff-tree -r --abbrev initial
diff-tree -r --abbrev=4 initial
diff-tree --root initial
diff-tree --root --abbrev initial
:noellipses diff-tree --root --abbrev initial
diff-tree --root -r initial
diff-tree --root -r --abbrev initial
:noellipses diff-tree --root -r --abbrev initial
diff-tree --root -r --abbrev=4 initial
:noellipses diff-tree --root -r --abbrev=4 initial
diff-tree -p initial
diff-tree --root -p initial
diff-tree --root -p --abbrev=10 initial
diff-tree --root -p --full-index initial
diff-tree --root -p --full-index --abbrev=10 initial
diff-tree --patch-with-stat initial
diff-tree --root --patch-with-stat initial
diff-tree --patch-with-raw initial
diff-tree --root --patch-with-raw initial

diff-tree --pretty initial
diff-tree --pretty --root initial
diff-tree --pretty -p initial
diff-tree --pretty --stat initial
diff-tree --pretty --summary initial
diff-tree --pretty --stat --summary initial
diff-tree --pretty --root -p initial
diff-tree --pretty --root --stat initial
# improved by Timo's patch
diff-tree --pretty --root --summary initial
# improved by Timo's patch
diff-tree --pretty --root --summary -r initial
diff-tree --pretty --root --stat --summary initial
diff-tree --pretty --patch-with-stat initial
diff-tree --pretty --root --patch-with-stat initial
diff-tree --pretty --patch-with-raw initial
diff-tree --pretty --root --patch-with-raw initial

diff-tree --pretty=oneline initial
diff-tree --pretty=oneline --root initial
diff-tree --pretty=oneline -p initial
diff-tree --pretty=oneline --root -p initial
diff-tree --pretty=oneline --patch-with-stat initial
# improved by Timo's patch
diff-tree --pretty=oneline --root --patch-with-stat initial
diff-tree --pretty=oneline --patch-with-raw initial
diff-tree --pretty=oneline --root --patch-with-raw initial

diff-tree --pretty side
diff-tree --pretty -p side
diff-tree --pretty --patch-with-stat side

diff-tree initial mode
diff-tree --stat initial mode
diff-tree --summary initial mode

diff-tree main
diff-tree -m main
diff-tree -p main
diff-tree -p -m main
diff-tree -c main
diff-tree -c --abbrev main
:noellipses diff-tree -c --abbrev main
diff-tree --cc main
# stat only should show the diffstat with the first parent
diff-tree -c --stat main
diff-tree --cc --stat main
diff-tree -c --stat --summary main
diff-tree --cc --stat --summary main
# stat summary should show the diffstat and summary with the first parent
diff-tree -c --stat --summary side
diff-tree --cc --stat --summary side
diff-tree --cc --shortstat main
diff-tree --cc --summary REVERSE
# improved by Timo's patch
diff-tree --cc --patch-with-stat main
# improved by Timo's patch
diff-tree --cc --patch-with-stat --summary main
# this is correct
diff-tree --cc --patch-with-stat --summary side

log main
log -p main
log --root main
log --root -p main
log --patch-with-stat main
log --root --patch-with-stat main
log --root --patch-with-stat --summary main
# improved by Timo's patch
log --root -c --patch-with-stat --summary main
# improved by Timo's patch
log --root --cc --patch-with-stat --summary main
log --no-diff-merges -p --first-parent main
log --diff-merges=off -p --first-parent main
log --first-parent --diff-merges=off -p main
log -p --first-parent main
log -p --diff-merges=first-parent main
log --diff-merges=first-parent main
log -m -p --first-parent main
log -m -p main
log --cc -m -p main
log -c -m -p main
log -m --raw main
log -m --stat main
log -SF main
log -S F main
log -SF -p main
log -SF main --max-count=0
log -SF main --max-count=1
log -SF main --max-count=2
log -GF main
log -GF -p main
log -GF -p --pickaxe-all main
log -IA -IB -I1 -I2 -p main
log --decorate --all
log --decorate=full --all
log --decorate --clear-decorations --all
log --decorate=full --clear-decorations --all

rev-list --parents HEAD
rev-list --children HEAD

whatchanged main
:noellipses whatchanged main
whatchanged -p main
whatchanged --root main
:noellipses whatchanged --root main
whatchanged --root -p main
whatchanged --patch-with-stat main
whatchanged --root --patch-with-stat main
whatchanged --root --patch-with-stat --summary main
# improved by Timo's patch
whatchanged --root -c --patch-with-stat --summary main
# improved by Timo's patch
whatchanged --root --cc --patch-with-stat --summary main
whatchanged -SF main
:noellipses whatchanged -SF main
whatchanged -SF -p main

log --patch-with-stat main -- dir/
whatchanged --patch-with-stat main -- dir/
log --patch-with-stat --summary main -- dir/
whatchanged --patch-with-stat --summary main -- dir/

show initial
show --root initial
show side
show main
show -c main
show -m main
show --first-parent main
show --stat side
show --stat --summary side
show --patch-with-stat side
show --patch-with-raw side
:noellipses show --patch-with-raw side
show --patch-with-stat --summary side

format-patch --stdout initial..side
format-patch --stdout initial..main^
format-patch --stdout initial..main
format-patch --stdout --no-numbered initial..main
format-patch --stdout --numbered initial..main
format-patch --attach --stdout initial..side
format-patch --attach --stdout --suffix=.diff initial..side
format-patch --attach --stdout initial..main^
format-patch --attach --stdout initial..main
format-patch --inline --stdout initial..side
format-patch --inline --stdout initial..main^
format-patch --inline --stdout --numbered-files initial..main
format-patch --inline --stdout initial..main
format-patch --inline --stdout --subject-prefix=TESTCASE initial..main
config format.subjectprefix DIFFERENT_PREFIX
format-patch --inline --stdout initial..main^^
format-patch --stdout --cover-letter -n initial..main^

diff --abbrev initial..side
diff -U initial..side
diff -U1 initial..side
diff -r initial..side
diff --stat initial..side
diff -r --stat initial..side
diff initial..side
diff --patch-with-stat initial..side
diff --patch-with-raw initial..side
:noellipses diff --patch-with-raw initial..side
diff --patch-with-stat -r initial..side
diff --patch-with-raw -r initial..side
:noellipses diff --patch-with-raw -r initial..side
diff --name-status dir2 dir
diff --no-index --name-status dir2 dir
diff --no-index --name-status -- dir2 dir
diff --no-index dir dir3
diff main main^ side
# Can't use spaces...
diff --line-prefix=abc main main^ side
diff --dirstat main~1 main~2
diff --dirstat initial rearrange
diff --dirstat-by-file initial rearrange
diff --dirstat --cc main~1 main
# No-index --abbrev and --no-abbrev
diff --raw initial
:noellipses diff --raw initial
diff --raw --abbrev=4 initial
:noellipses diff --raw --abbrev=4 initial
diff --raw --no-abbrev initial
diff --no-index --raw dir2 dir
:noellipses diff --no-index --raw dir2 dir
diff --no-index --raw --abbrev=4 dir2 dir
:noellipses diff --no-index --raw --abbrev=4 dir2 dir
diff --no-index --raw --no-abbrev dir2 dir

diff-tree --pretty --root --stat --compact-summary initial
diff-tree --pretty -R --root --stat --compact-summary initial
diff-tree --pretty note
diff-tree --pretty --notes note
diff-tree --format=%N note
diff-tree --stat --compact-summary initial mode
diff-tree -R --stat --compact-summary initial mode
EOF

test_expect_success !WITH_BREAKING_CHANGES 'whatchanged needs --i-still-use-this' '
	test_must_fail git whatchanged >message 2>&1 &&
	test_grep "nominated for removal" message
'

test_expect_success 'log -m matches pure log' '
	git log main >result &&
	process_diffs result >expected &&
	git log -m >result &&
	process_diffs result >actual &&
	test_cmp expected actual
'

test_expect_success 'log --diff-merges=on matches --diff-merges=separate' '
	git log -p --diff-merges=separate main >result &&
	process_diffs result >expected &&
	git log -p --diff-merges=on main >result &&
	process_diffs result >actual &&
	test_cmp expected actual
'

test_expect_success 'log --dd matches --diff-merges=1 -p' '
	git log --diff-merges=1 -p main >result &&
	process_diffs result >expected &&
	git log --dd main >result &&
	process_diffs result >actual &&
	test_cmp expected actual
'

test_expect_success 'deny wrong log.diffMerges config' '
	test_config log.diffMerges wrong-value &&
	test_expect_code 128 git log
'

test_expect_success 'git config log.diffMerges first-parent' '
	git log -p --diff-merges=first-parent main >result &&
	process_diffs result >expected &&
	test_config log.diffMerges first-parent &&
	git log -p --diff-merges=on main >result &&
	process_diffs result >actual &&
	test_cmp expected actual
'

test_expect_success 'git config log.diffMerges first-parent vs -m' '
	git log -p --diff-merges=first-parent main >result &&
	process_diffs result >expected &&
	test_config log.diffMerges first-parent &&
	git log -p -m main >result &&
	process_diffs result >actual &&
	test_cmp expected actual
'

# -m in "git diff-index" means "match missing", that differs
# from its meaning in "git diff". Let's check it in diff-index.
# The line in the output for removed file should disappear when
# we provide -m in diff-index.
test_expect_success 'git diff-index -m' '
	rm -f file1 &&
	git diff-index HEAD >without-m &&
	lines_count=$(wc -l <without-m) &&
	git diff-index -m HEAD >with-m &&
	git restore file1 &&
	test_line_count = $((lines_count - 1)) with-m
'

test_expect_success 'log -S requires an argument' '
	test_must_fail git log -S
'

test_expect_success 'diff --cached on unborn branch' '
	git symbolic-ref HEAD refs/heads/unborn &&
	git diff --cached >result &&
	process_diffs result >actual &&
	process_diffs "$TEST_DIRECTORY/t4013/diff.diff_--cached" >expected &&
	test_cmp expected actual
'

test_expect_success 'diff --cached -- file on unborn branch' '
	git diff --cached -- file0 >result &&
	process_diffs result >actual &&
	process_diffs "$TEST_DIRECTORY/t4013/diff.diff_--cached_--_file0" >expected &&
	test_cmp expected actual
'
test_expect_success 'diff --line-prefix with spaces' '
	git diff --line-prefix="| | | " --cached -- file0 >result &&
	process_diffs result >actual &&
	process_diffs "$TEST_DIRECTORY/t4013/diff.diff_--line-prefix_--cached_--_file0" >expected &&
	test_cmp expected actual
'

test_expect_success 'diff-tree --stdin with log formatting' '
	cat >expect <<-\EOF &&
	Side
	Third
	Second
	EOF
	git rev-list main | git diff-tree --stdin --format=%s -s >actual &&
	test_cmp expect actual
'

test_expect_success 'diff-tree --stdin with pathspec' '
	cat >expect <<-EOF &&
	Third

	dir/sub
	Second

	dir/sub
	EOF
	git rev-list main^ |
	git diff-tree -r --stdin --name-only --format=%s dir >actual &&
	test_cmp expect actual
'

test_expect_success 'show A B ... -- <pathspec>' '
	# side touches dir/sub, file0, and file3
	# main^ touches dir/sub, and file1
	# main^^ touches dir/sub, file0, and file2
	git show --name-only --format="<%s>" side main^ main^^ -- dir >actual &&
	cat >expect <<-\EOF &&
	<Side>

	dir/sub
	<Third>

	dir/sub
	<Second>

	dir/sub
	EOF
	test_cmp expect actual
'

test_expect_success 'diff -I<regex>: setup' '
	git checkout main &&
	test_seq 50 >file0 &&
	git commit -m "Set up -I<regex> test file" file0 &&
	test_seq 50 | sed -e "s/13/ten and three/" -e "/7\$/d" >file0 &&
	echo >>file0
'
test_expect_success 'diff -I<regex>' '
	git diff --ignore-blank-lines -I"ten.*e" -I"^[124-9]" >actual &&
	cat >expect <<-\EOF &&
	diff --git a/file0 b/file0
	--- a/file0
	+++ b/file0
	@@ -34,7 +31,6 @@
	 34
	 35
	 36
	-37
	 38
	 39
	 40
	EOF
	compare_diff_patch expect actual
'

test_expect_success 'diff -I<regex> --stat' '
	git diff --stat --ignore-blank-lines -I"ten.*e" -I"^[124-9]" >actual &&
	cat >expect <<-\EOF &&
	 file0 | 1 -
	 1 file changed, 1 deletion(-)
	EOF
	test_cmp expect actual
'

test_expect_success 'diff -I<regex>: detect malformed regex' '
	test_expect_code 129 git diff --ignore-matching-lines="^[124-9" 2>error &&
	test_grep "invalid regex given to -I: " error
'

test_expect_success 'diff -I<regex>: ignore matching file' '
	test_when_finished "git rm -f file1" &&
	test_seq 50 >file1 &&
	git add file1 &&
	test_seq 50 | sed -e "s/13/ten and three/" -e "s/^[124-9].*/& /" >file1 &&

	: >actual &&
	git diff --raw --ignore-blank-lines -I"ten.*e" -I"^[124-9]" >>actual &&
	git diff --name-only --ignore-blank-lines -I"ten.*e" -I"^[124-9]" >>actual &&
	git diff --name-status --ignore-blank-lines -I"ten.*e" -I"^[124-9]" >>actual &&
	test_grep ! "file1" actual
'

# check_prefix <patch> <src> <dst>
# check only lines with paths to avoid dependency on exact oid/contents
check_prefix () {
	grep -E '^(diff|---|\+\+\+) ' "$1" >actual.paths &&
	cat >expect <<-EOF &&
	diff --git $2 $3
	--- $2
	+++ $3
	EOF
	test_cmp expect actual.paths
}

test_expect_success 'diff-files does not respect diff.noPrefix' '
	git -c diff.noPrefix diff-files -p >actual &&
	check_prefix actual a/file0 b/file0
'

test_expect_success 'diff-files respects --no-prefix' '
	git diff-files -p --no-prefix >actual &&
	check_prefix actual file0 file0
'

test_expect_success 'diff respects diff.noPrefix' '
	git -c diff.noPrefix diff >actual &&
	check_prefix actual file0 file0
'

test_expect_success 'diff --default-prefix overrides diff.noPrefix' '
	git -c diff.noPrefix diff --default-prefix >actual &&
	check_prefix actual a/file0 b/file0
'

test_expect_success 'diff respects diff.mnemonicPrefix' '
	git -c diff.mnemonicPrefix diff >actual &&
	check_prefix actual i/file0 w/file0
'

test_expect_success 'diff --default-prefix overrides diff.mnemonicPrefix' '
	git -c diff.mnemonicPrefix diff --default-prefix >actual &&
	check_prefix actual a/file0 b/file0
'

test_expect_success 'diff respects diff.srcPrefix' '
	git -c diff.srcPrefix=x/ diff >actual &&
	check_prefix actual x/file0 b/file0
'

test_expect_success 'diff respects diff.dstPrefix' '
	git -c diff.dstPrefix=y/ diff >actual &&
	check_prefix actual a/file0 y/file0
'

test_expect_success 'diff --src-prefix overrides diff.srcPrefix' '
	git -c diff.srcPrefix=y/ diff --src-prefix=z/ >actual &&
	check_prefix actual z/file0 b/file0
'

test_expect_success 'diff --dst-prefix overrides diff.dstPrefix' '
	git -c diff.dstPrefix=y/ diff --dst-prefix=z/ >actual &&
	check_prefix actual a/file0 z/file0
'

test_expect_success 'diff.{src,dst}Prefix ignored with diff.noPrefix' '
	git -c diff.dstPrefix=y/ -c diff.srcPrefix=x/ -c diff.noPrefix diff >actual &&
	check_prefix actual file0 file0
'

test_expect_success 'diff.{src,dst}Prefix ignored with diff.mnemonicPrefix' '
	git -c diff.dstPrefix=x/ -c diff.srcPrefix=y/ -c diff.mnemonicPrefix diff >actual &&
	check_prefix actual i/file0 w/file0
'

test_expect_success 'diff.{src,dst}Prefix ignored with --default-prefix' '
	git -c diff.dstPrefix=x/ -c diff.srcPrefix=y/ diff --default-prefix >actual &&
	check_prefix actual a/file0 b/file0
'

test_expect_success 'diff --no-renames cannot be abbreviated' '
	test_expect_code 129 git diff --no-rename >actual 2>error &&
	test_must_be_empty actual &&
	grep "invalid option: --no-rename" error
'

test_done
