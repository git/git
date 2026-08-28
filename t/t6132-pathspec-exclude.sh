#!/bin/sh

test_description='test case exclude pathspec'

. ./test-lib.sh

test_expect_success 'setup' '
	for p in file sub/file sub/sub/file sub/file2 sub/sub/sub/file sub2/file; do
		if echo $p | grep /; then
			mkdir -p $(dirname $p)
		fi &&
		: >$p &&
		git add $p &&
		git commit -m $p || return 1
	done &&
	git log --oneline --format=%s >actual &&
	cat <<EOF >expect &&
sub2/file
sub/sub/sub/file
sub/file2
sub/sub/file
sub/file
file
EOF
	test_cmp expect actual
'

test_expect_success 'exclude only pathspec uses default implicit pathspec' '
	git log --oneline --format=%s -- . ":(exclude)sub" >expect &&
	git log --oneline --format=%s -- ":(exclude)sub" >actual &&
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude sub' '
	git log --oneline --format=%s -- . ":(exclude)sub" >actual &&
	cat <<EOF >expect &&
sub2/file
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude sub/sub/file' '
	git log --oneline --format=%s -- . ":(exclude)sub/sub/file" >actual &&
	cat <<EOF >expect &&
sub2/file
sub/sub/sub/file
sub/file2
sub/file
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude sub using mnemonic' '
	git log --oneline --format=%s -- . ":!sub" >actual &&
	cat <<EOF >expect &&
sub2/file
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude :(icase)SUB' '
	git log --oneline --format=%s -- . ":(exclude,icase)SUB" >actual &&
	cat <<EOF >expect &&
sub2/file
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude sub2 from sub' '
	(
	cd sub &&
	git log --oneline --format=%s -- :/ ":/!sub2" >actual &&
	cat <<EOF >expect &&
sub/sub/sub/file
sub/file2
sub/sub/file
sub/file
file
EOF
	test_cmp expect actual
	)
'

test_expect_success 't_e_i() exclude sub/*file' '
	git log --oneline --format=%s -- . ":(exclude)sub/*file" >actual &&
	cat <<EOF >expect &&
sub2/file
sub/file2
file
EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude :(glob)sub/*/file' '
	git log --oneline --format=%s -- . ":(exclude,glob)sub/*/file" >actual &&
	cat <<EOF >expect &&
sub2/file
sub/sub/sub/file
sub/file2
sub/file
file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude sub' '
	git ls-files -- . ":(exclude)sub" >actual &&
	cat <<EOF >expect &&
file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude sub/sub/file' '
	git ls-files -- . ":(exclude)sub/sub/file" >actual &&
	cat <<EOF >expect &&
file
sub/file
sub/file2
sub/sub/sub/file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude sub using mnemonic' '
	git ls-files -- . ":!sub" >actual &&
	cat <<EOF >expect &&
file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude :(icase)SUB' '
	git ls-files -- . ":(exclude,icase)SUB" >actual &&
	cat <<EOF >expect &&
file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude sub2 from sub' '
	(
	cd sub &&
	git ls-files -- :/ ":/!sub2" >actual &&
	cat <<EOF >expect &&
../file
file
file2
sub/file
sub/sub/file
EOF
	test_cmp expect actual
	)
'

test_expect_success 'm_p_d() exclude sub/*file' '
	git ls-files -- . ":(exclude)sub/*file" >actual &&
	cat <<EOF >expect &&
file
sub/file2
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'm_p_d() exclude :(glob)sub/*/file' '
	git ls-files -- . ":(exclude,glob)sub/*/file" >actual &&
	cat <<EOF >expect &&
file
sub/file
sub/file2
sub/sub/sub/file
sub2/file
EOF
	test_cmp expect actual
'

test_expect_success 'multiple exclusions' '
	git ls-files -- ":^*/file2" ":^sub2" >actual &&
	cat <<-\EOF >expect &&
	file
	sub/file
	sub/sub/file
	sub/sub/sub/file
	EOF
	test_cmp expect actual
'

test_expect_success 't_e_i() exclude case #8' '
	test_when_finished "rm -fr case8" &&
	git init case8 &&
	(
		cd case8 &&
		echo file >file1 &&
		echo file >file2 &&
		git add file1 file2 &&
		git commit -m twofiles &&
		git grep -l file HEAD :^file2 >actual &&
		echo HEAD:file1 >expected &&
		test_cmp expected actual &&
		git grep -l file HEAD :^file1 >actual &&
		echo HEAD:file2 >expected &&
		test_cmp expected actual
	)
'

test_expect_success 'grep --untracked PATTERN' '
	# This test is not an actual test of exclude patterns, rather it
	# is here solely to ensure that if any tests are inserted, deleted, or
	# changed above, that we still have untracked files with the expected
	# contents for the NEXT two tests.
	cat <<-\EOF >expect-grep &&
	actual
	expect
	sub/actual
	sub/expect
	EOF
	git grep -l --untracked file -- >actual-grep &&
	test_cmp expect-grep actual-grep
'

test_expect_success 'grep --untracked PATTERN :(exclude)DIR' '
	cat <<-\EOF >expect-grep &&
	actual
	expect
	EOF
	git grep -l --untracked file -- ":(exclude)sub" >actual-grep &&
	test_cmp expect-grep actual-grep
'

test_expect_success 'grep --untracked PATTERN :(exclude)*FILE' '
	cat <<-\EOF >expect-grep &&
	actual
	sub/actual
	EOF
	git grep -l --untracked file -- ":(exclude)*expect" >actual-grep &&
	test_cmp expect-grep actual-grep
'

# Depending on the command, all negative pathspec needs to subtract
# either from the full tree, or from the current directory.
#
# The sample tree checked out at this point has:
# file
# sub/file
# sub/file2
# sub/sub/file
# sub/sub/sub/file
# sub2/file
#
# but there may also be some cruft that interferes with "git clean"
# and "git add" tests.

test_expect_success 'archive with all negative' '
	git reset --hard &&
	git clean -f &&
	git -C sub archive --format=tar HEAD -- ":!sub/" >archive &&
	"$TAR" tf archive >actual &&
	cat >expect <<-\EOF &&
	file
	file2
	EOF
	test_cmp expect actual
'

test_expect_success 'add with all negative' '
	H=$(git rev-parse HEAD) &&
	git reset --hard $H &&
	git clean -f &&
	test_when_finished "git reset --hard $H" &&
	for path in file sub/file sub/sub/file sub2/file
	do
		echo smudge >>"$path" || return 1
	done &&
	git -C sub add -- ":!sub/" &&
	git diff --name-only --no-renames --cached >actual &&
	cat >expect <<-\EOF &&
	file
	sub/file
	sub2/file
	EOF
	test_cmp expect actual &&
	git diff --name-only --no-renames >actual &&
	echo sub/sub/file >expect &&
	test_cmp expect actual
'

test_expect_success 'add -p with all negative' '
	H=$(git rev-parse HEAD) &&
	git reset --hard $H &&
	git clean -f &&
	test_when_finished "git reset --hard $H" &&
	for path in file sub/file sub/sub/file sub2/file
	do
		echo smudge >>"$path" || return 1
	done &&
	yes | git -C sub add -p -- ":!sub/" &&
	git diff --name-only --no-renames --cached >actual &&
	cat >expect <<-\EOF &&
	file
	sub/file
	sub2/file
	EOF
	test_cmp expect actual &&
	git diff --name-only --no-renames >actual &&
	echo sub/sub/file >expect &&
	test_cmp expect actual
'

test_expect_success 'clean with all negative' '
	H=$(git rev-parse HEAD) &&
	git reset --hard $H &&
	test_when_finished "git reset --hard $H && git clean -f" &&
	git clean -f &&
	for path in file9 sub/file9 sub/sub/file9 sub2/file9
	do
		echo cruft >"$path" || return 1
	done &&
	git -C sub clean -f -- ":!sub" &&
	test_path_is_file file9 &&
	test_path_is_missing sub/file9 &&
	test_path_is_file sub/sub/file9 &&
	test_path_is_file sub2/file9
'

test_expect_success 'commit with all negative' '
	H=$(git rev-parse HEAD) &&
	git reset --hard $H &&
	test_when_finished "git reset --hard $H" &&
	for path in file sub/file sub/sub/file sub2/file
	do
		echo smudge >>"$path" || return 1
	done &&
	git -C sub commit -m sample -- ":!sub/" &&
	git diff --name-only --no-renames HEAD^ HEAD >actual &&
	cat >expect <<-\EOF &&
	file
	sub/file
	sub2/file
	EOF
	test_cmp expect actual &&
	git diff --name-only --no-renames HEAD >actual &&
	echo sub/sub/file >expect &&
	test_cmp expect actual
'

test_expect_success 'reset with all negative' '
	H=$(git rev-parse HEAD) &&
	git reset --hard $H &&
	test_when_finished "git reset --hard $H" &&
	for path in file sub/file sub/sub/file sub2/file
	do
		echo smudge >>"$path" &&
		git add "$path" || return 1
	done &&
	git -C sub reset --quiet -- ":!sub/" &&
	git diff --name-only --no-renames --cached >actual &&
	echo sub/sub/file >expect &&
	test_cmp expect actual
'

test_expect_success 'grep with all negative' '
	H=$(git rev-parse HEAD) &&
	git reset --hard $H &&
	test_when_finished "git reset --hard $H" &&
	for path in file sub/file sub/sub/file sub2/file
	do
		echo "needle $path" >>"$path" || return 1
	done &&
	git -C sub grep -h needle -- ":!sub/" >actual &&
	cat >expect <<-\EOF &&
	needle sub/file
	EOF
	test_cmp expect actual
'

test_expect_success 'ls-files with all negative' '
	git reset --hard &&
	git -C sub ls-files -- ":!sub/" >actual &&
	cat >expect <<-\EOF &&
	file
	file2
	EOF
	test_cmp expect actual
'

test_expect_success 'rm with all negative' '
	git reset --hard &&
	test_when_finished "git reset --hard" &&
	git -C sub rm -r --cached -- ":!sub/" >actual &&
	git diff --name-only --no-renames --diff-filter=D --cached >actual &&
	cat >expect <<-\EOF &&
	sub/file
	sub/file2
	EOF
	test_cmp expect actual
'

test_expect_success 'stash with all negative' '
	H=$(git rev-parse HEAD) &&
	git reset --hard $H &&
	test_when_finished "git reset --hard $H" &&
	for path in file sub/file sub/sub/file sub2/file
	do
		echo smudge >>"$path" || return 1
	done &&
	git -C sub stash push -m sample -- ":!sub/" &&
	git diff --name-only --no-renames HEAD >actual &&
	echo sub/sub/file >expect &&
	test_cmp expect actual &&
	git stash show --name-only >actual &&
	cat >expect <<-\EOF &&
	file
	sub/file
	sub2/file
	EOF
	test_cmp expect actual
'

# `ls-files` finds the length of the common prefix of the *positive* pathspecs.
# In this example, there's only one positive pathspec, so the common prefix is `aaa/bbb`, with length 7.
#
# Before the bug described in https://lore.kernel.org/git/e2dbe996f6a7285fe0487e34d65eccf712867547.camel@redhat.com
# was patched, as an optimization, we would then strip the first 7 characters from the path,
# the positive pathspec, and (incorrectly) the negative pathspec.
#
# But stripping the negative pathspec would mean that `xxx/yyy/file` becomes `file`
# and we'd wrongly end up excluding `aaa/bbb/file`.
#
# After this bug fix, `aaa/bbb/file` should no longer be excluded by `:(exclude)xxx/yyy/file`.
test_expect_success 'exclude is not matched against the tail of the path' '
	test_when_finished "git rm -q --cached -r aaa xxx && rm -rf aaa xxx" &&
	mkdir -p aaa/bbb xxx/yyy &&
	>aaa/bbb/file &&
	>xxx/yyy/other &&
	git add aaa xxx &&
	echo aaa/bbb/file >expect &&
	git ls-files -- aaa/bbb/file ":(exclude)xxx/yyy/file" >actual &&
	test_cmp expect actual
'

# Before the bug described in https://lore.kernel.org/git/e2dbe996f6a7285fe0487e34d65eccf712867547.camel@redhat.com
# was patched, when the negative pathspec had the same length or was
# shorter than the common prefix of the positive pathspecs,
# then stripping the common prefix from the negative pathspec would result in an empty string,
# which would match everything, and thus exclude all files.
#
# In this test, the prefix for "sub/sub/sub/file" is "sub/sub/sub" (11 bytes).
test_expect_success 'ls-files keeps entries when an exclude matches the common prefix length' '
	echo sub/sub/sub/file >expect &&
	git ls-files -- sub/sub/sub/file ":(exclude)nonexistent" >actual &&
	test_cmp expect actual
'

# This test is similar to the above, but tests `git add` instead of `git ls-files`.
#
# `git add` does not exclude the trailing slash, so the common prefix is "sub/sub/sub/" (12 bytes).
test_expect_success 'add keeps entries when an exclude matches the common prefix length' '
	test_when_finished "git reset -q && rm -f sub/sub/sub/untracked" &&
	>sub/sub/sub/untracked &&
	git add -- sub/sub/sub/ ":(exclude)no/such/path" &&
	echo sub/sub/sub/untracked >expect &&
	git diff --cached --name-only HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'an exclude shorter than the common prefix still excludes' '
	git ls-files -- sub/sub/sub/file ":(exclude)sub" >actual &&
	test_must_be_empty actual
'

test_done
