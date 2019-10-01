#!/bin/sh

test_description='Test cherry-pick -x and -s'

. ./test-lib.sh

pristine_detach () {
	git cherry-pick --quit &&
	git checkout -f "$1^0" &&
	git read-tree -u --reset HEAD &&
	git clean -d -f -f -q -x
}

mesg_one_line='base: commit message'

mesg_no_footer="$mesg_one_line

OneWordBodyThatsNotA-S-o-B"

mesg_with_footer="$mesg_no_footer

Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
Signed-off-by: A.U. Thor <author@example.com>
Signed-off-by: B.U. Thor <buthor@example.com>"

mesg_broken_footer="$mesg_no_footer

This is not recognized as a footer because Myfooter is not a recognized token.
Myfooter: A.U. Thor <author@example.com>"

mesg_with_footer_sob="$mesg_with_footer
Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"

mesg_with_cherry_footer="$mesg_with_footer_sob
(cherry picked from commit da39a3ee5e6b4b0d3255bfef95601890afd80709)
Tested-by: C.U. Thor <cuthor@example.com>"

mesg_unclean="$mesg_one_line


leading empty lines


consecutive empty lines

# hash tag comment

trailing empty lines


"

test_expect_success setup '
	git config advice.detachedhead false &&
	echo unrelated >unrelated &&
	git add unrelated &&
	test_commit initial foo a &&
	test_commit "$mesg_one_line" foo b mesg-one-line &&
	git reset --hard initial &&
	test_commit "$mesg_no_footer" foo b mesg-no-footer &&
	git reset --hard initial &&
	test_commit "$mesg_broken_footer" foo b mesg-broken-footer &&
	git reset --hard initial &&
	test_commit "$mesg_with_footer" foo b mesg-with-footer &&
	git reset --hard initial &&
	test_commit "$mesg_with_footer_sob" foo b mesg-with-footer-sob &&
	git reset --hard initial &&
	test_commit "$mesg_with_cherry_footer" foo b mesg-with-cherry-footer &&
	git reset --hard initial &&
	test_config commit.cleanup verbatim &&
	test_commit "$mesg_unclean" foo b mesg-unclean &&
	test_unconfig commit.cleanup &&
	pristine_detach initial &&
	test_commit conflicting unrelated
'

test_expect_success 'cherry-pick -x inserts blank line after one line subject' '
	pristine_detach initial &&
	sha1=$(git rev-parse mesg-one-line^0) &&
	git cherry-pick -x mesg-one-line &&
	cat <<-EOF >expect &&
		$mesg_one_line

		(cherry picked from commit $sha1)
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s inserts blank line after one line subject' '
	pristine_detach initial &&
	git cherry-pick -s mesg-one-line &&
	cat <<-EOF >expect &&
		$mesg_one_line

		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s inserts blank line after non-conforming footer' '
	pristine_detach initial &&
	git cherry-pick -s mesg-broken-footer &&
	cat <<-EOF >expect &&
		$mesg_broken_footer

		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s recognizes trailer config' '
	pristine_detach initial &&
	git -c "trailer.Myfooter.ifexists=add" cherry-pick -s mesg-broken-footer &&
	cat <<-EOF >expect &&
		$mesg_broken_footer
		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x inserts blank line when conforming footer not found' '
	pristine_detach initial &&
	sha1=$(git rev-parse mesg-no-footer^0) &&
	git cherry-pick -x mesg-no-footer &&
	cat <<-EOF >expect &&
		$mesg_no_footer

		(cherry picked from commit $sha1)
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s inserts blank line when conforming footer not found' '
	pristine_detach initial &&
	git cherry-pick -s mesg-no-footer &&
	cat <<-EOF >expect &&
		$mesg_no_footer

		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x -s inserts blank line when conforming footer not found' '
	pristine_detach initial &&
	sha1=$(git rev-parse mesg-no-footer^0) &&
	git cherry-pick -x -s mesg-no-footer &&
	cat <<-EOF >expect &&
		$mesg_no_footer

		(cherry picked from commit $sha1)
		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s adds sob when last sob doesnt match committer' '
	pristine_detach initial &&
	git cherry-pick -s mesg-with-footer &&
	cat <<-EOF >expect &&
		$mesg_with_footer
		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x -s adds sob when last sob doesnt match committer' '
	pristine_detach initial &&
	sha1=$(git rev-parse mesg-with-footer^0) &&
	git cherry-pick -x -s mesg-with-footer &&
	cat <<-EOF >expect &&
		$mesg_with_footer
		(cherry picked from commit $sha1)
		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s refrains from adding duplicate trailing sob' '
	pristine_detach initial &&
	git cherry-pick -s mesg-with-footer-sob &&
	cat <<-EOF >expect &&
		$mesg_with_footer_sob
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x -s adds sob even when trailing sob exists for committer' '
	pristine_detach initial &&
	sha1=$(git rev-parse mesg-with-footer-sob^0) &&
	git cherry-pick -x -s mesg-with-footer-sob &&
	cat <<-EOF >expect &&
		$mesg_with_footer_sob
		(cherry picked from commit $sha1)
		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x handles commits with no NL at end of message' '
	pristine_detach initial &&
	printf "title\n\nSigned-off-by: A <a@example.com>" >msg &&
	sha1=$(git commit-tree -p initial mesg-with-footer^{tree} <msg) &&
	git cherry-pick -x $sha1 &&
	git log -1 --pretty=format:%B >actual &&

	printf "\n(cherry picked from commit %s)\n" $sha1 >>msg &&
	test_cmp msg actual
'

test_expect_success 'cherry-pick -x handles commits with no footer and no NL at end of message' '
	pristine_detach initial &&
	printf "title\n\nnot a footer" >msg &&
	sha1=$(git commit-tree -p initial mesg-with-footer^{tree} <msg) &&
	git cherry-pick -x $sha1 &&
	git log -1 --pretty=format:%B >actual &&

	printf "\n\n(cherry picked from commit %s)\n" $sha1 >>msg &&
	test_cmp msg actual
'

test_expect_success 'cherry-pick -s handles commits with no NL at end of message' '
	pristine_detach initial &&
	printf "title\n\nSigned-off-by: A <a@example.com>" >msg &&
	sha1=$(git commit-tree -p initial mesg-with-footer^{tree} <msg) &&
	git cherry-pick -s $sha1 &&
	git log -1 --pretty=format:%B >actual &&

	printf "\nSigned-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>\n" >>msg &&
	test_cmp msg actual
'

test_expect_success 'cherry-pick -s handles commits with no footer and no NL at end of message' '
	pristine_detach initial &&
	printf "title\n\nnot a footer" >msg &&
	sha1=$(git commit-tree -p initial mesg-with-footer^{tree} <msg) &&
	git cherry-pick -s $sha1 &&
	git log -1 --pretty=format:%B >actual &&

	printf "\n\nSigned-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>\n" >>msg &&
	test_cmp msg actual
'

test_expect_success 'cherry-pick -x treats "(cherry picked from..." line as part of footer' '
	pristine_detach initial &&
	sha1=$(git rev-parse mesg-with-cherry-footer^0) &&
	git cherry-pick -x mesg-with-cherry-footer &&
	cat <<-EOF >expect &&
		$mesg_with_cherry_footer
		(cherry picked from commit $sha1)
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s treats "(cherry picked from..." line as part of footer' '
	pristine_detach initial &&
	git cherry-pick -s mesg-with-cherry-footer &&
	cat <<-EOF >expect &&
		$mesg_with_cherry_footer
		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x -s treats "(cherry picked from..." line as part of footer' '
	pristine_detach initial &&
	sha1=$(git rev-parse mesg-with-cherry-footer^0) &&
	git cherry-pick -x -s mesg-with-cherry-footer &&
	cat <<-EOF >expect &&
		$mesg_with_cherry_footer
		(cherry picked from commit $sha1)
		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick preserves commit message' '
	pristine_detach initial &&
	printf "$mesg_unclean" >expect &&
	git log -1 --pretty=format:%B mesg-unclean >actual &&
	test_cmp expect actual &&
	git cherry-pick mesg-unclean &&
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x cleans commit message' '
	pristine_detach initial &&
	git cherry-pick -x mesg-unclean &&
	git log -1 --pretty=format:%B >actual &&
	printf "%s\n(cherry picked from commit %s)\n" \
		"$mesg_unclean" $(git rev-parse mesg-unclean) |
			git stripspace >expect &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x respects commit.cleanup' '
	pristine_detach initial &&
	git -c commit.cleanup=strip cherry-pick -x mesg-unclean &&
	git log -1 --pretty=format:%B >actual &&
	printf "%s\n(cherry picked from commit %s)\n" \
		"$mesg_unclean" $(git rev-parse mesg-unclean) |
			git stripspace -s >expect &&
	test_cmp expect actual
'

test_done
