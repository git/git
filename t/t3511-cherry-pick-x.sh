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

The signed-off-by string should begin with the words Signed-off-by followed
by a colon and space, and then the signers name and email address. e.g.
Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"

mesg_with_footer_sob="$mesg_with_footer
Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"


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
	pristine_detach initial &&
	test_commit conflicting unrelated
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

test_expect_failure 'cherry-pick -s inserts blank line after non-conforming footer' '
	pristine_detach initial &&
	git cherry-pick -s mesg-broken-footer &&
	cat <<-EOF >expect &&
		$mesg_broken_footer

		Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
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

test_expect_success 'cherry-pick -s refrains from adding duplicate trailing sob' '
	pristine_detach initial &&
	git cherry-pick -s mesg-with-footer-sob &&
	cat <<-EOF >expect &&
		$mesg_with_footer_sob
	EOF
	git log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_done
