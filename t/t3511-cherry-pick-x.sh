#!/bin/sh

test_description='Test cherry-pick -x and -s'

. ./test-lib.sh

pristine_detach () {
	but cherry-pick --quit &&
	but checkout -f "$1^0" &&
	but read-tree -u --reset HEAD &&
	but clean -d -f -f -q -x
}

mesg_one_line='base: cummit message'

mesg_no_footer="$mesg_one_line

OneWordBodyThatsNotA-S-o-B"

mesg_with_footer="$mesg_no_footer

Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
Signed-off-by: A.U. Thor <author@example.com>
Signed-off-by: B.U. Thor <buthor@example.com>"

mesg_broken_footer="$mesg_no_footer

This is not recognized as a footer because Myfooter is not a recognized token.
Myfooter: A.U. Thor <author@example.com>"

mesg_with_footer_sob="$mesg_with_footer
Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>"

mesg_with_cherry_footer="$mesg_with_footer_sob
(cherry picked from cummit da39a3ee5e6b4b0d3255bfef95601890afd80709)
Tested-by: C.U. Thor <cuthor@example.com>"

mesg_unclean="$mesg_one_line


leading empty lines


consecutive empty lines

# hash tag comment

trailing empty lines


"

test_expect_success setup '
	but config advice.detachedhead false &&
	echo unrelated >unrelated &&
	but add unrelated &&
	test_cummit initial foo a &&
	test_cummit "$mesg_one_line" foo b mesg-one-line &&
	but reset --hard initial &&
	test_cummit "$mesg_no_footer" foo b mesg-no-footer &&
	but reset --hard initial &&
	test_cummit "$mesg_broken_footer" foo b mesg-broken-footer &&
	but reset --hard initial &&
	test_cummit "$mesg_with_footer" foo b mesg-with-footer &&
	but reset --hard initial &&
	test_cummit "$mesg_with_footer_sob" foo b mesg-with-footer-sob &&
	but reset --hard initial &&
	test_cummit "$mesg_with_cherry_footer" foo b mesg-with-cherry-footer &&
	but reset --hard initial &&
	test_config cummit.cleanup verbatim &&
	test_cummit "$mesg_unclean" foo b mesg-unclean &&
	test_unconfig cummit.cleanup &&
	pristine_detach initial &&
	test_cummit conflicting unrelated
'

test_expect_success 'cherry-pick -x inserts blank line after one line subject' '
	pristine_detach initial &&
	sha1=$(but rev-parse mesg-one-line^0) &&
	but cherry-pick -x mesg-one-line &&
	cat <<-EOF >expect &&
		$mesg_one_line

		(cherry picked from cummit $sha1)
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s inserts blank line after one line subject' '
	pristine_detach initial &&
	but cherry-pick -s mesg-one-line &&
	cat <<-EOF >expect &&
		$mesg_one_line

		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s inserts blank line after non-conforming footer' '
	pristine_detach initial &&
	but cherry-pick -s mesg-broken-footer &&
	cat <<-EOF >expect &&
		$mesg_broken_footer

		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s recognizes trailer config' '
	pristine_detach initial &&
	but -c "trailer.Myfooter.ifexists=add" cherry-pick -s mesg-broken-footer &&
	cat <<-EOF >expect &&
		$mesg_broken_footer
		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x inserts blank line when conforming footer not found' '
	pristine_detach initial &&
	sha1=$(but rev-parse mesg-no-footer^0) &&
	but cherry-pick -x mesg-no-footer &&
	cat <<-EOF >expect &&
		$mesg_no_footer

		(cherry picked from cummit $sha1)
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s inserts blank line when conforming footer not found' '
	pristine_detach initial &&
	but cherry-pick -s mesg-no-footer &&
	cat <<-EOF >expect &&
		$mesg_no_footer

		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x -s inserts blank line when conforming footer not found' '
	pristine_detach initial &&
	sha1=$(but rev-parse mesg-no-footer^0) &&
	but cherry-pick -x -s mesg-no-footer &&
	cat <<-EOF >expect &&
		$mesg_no_footer

		(cherry picked from cummit $sha1)
		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s adds sob when last sob doesnt match cummitter' '
	pristine_detach initial &&
	but cherry-pick -s mesg-with-footer &&
	cat <<-EOF >expect &&
		$mesg_with_footer
		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x -s adds sob when last sob doesnt match cummitter' '
	pristine_detach initial &&
	sha1=$(but rev-parse mesg-with-footer^0) &&
	but cherry-pick -x -s mesg-with-footer &&
	cat <<-EOF >expect &&
		$mesg_with_footer
		(cherry picked from cummit $sha1)
		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s refrains from adding duplicate trailing sob' '
	pristine_detach initial &&
	but cherry-pick -s mesg-with-footer-sob &&
	cat <<-EOF >expect &&
		$mesg_with_footer_sob
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x -s adds sob even when trailing sob exists for cummitter' '
	pristine_detach initial &&
	sha1=$(but rev-parse mesg-with-footer-sob^0) &&
	but cherry-pick -x -s mesg-with-footer-sob &&
	cat <<-EOF >expect &&
		$mesg_with_footer_sob
		(cherry picked from cummit $sha1)
		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x handles cummits with no NL at end of message' '
	pristine_detach initial &&
	printf "title\n\nSigned-off-by: A <a@example.com>" >msg &&
	sha1=$(but cummit-tree -p initial mesg-with-footer^{tree} <msg) &&
	but cherry-pick -x $sha1 &&
	but log -1 --pretty=format:%B >actual &&

	printf "\n(cherry picked from cummit %s)\n" $sha1 >>msg &&
	test_cmp msg actual
'

test_expect_success 'cherry-pick -x handles cummits with no footer and no NL at end of message' '
	pristine_detach initial &&
	printf "title\n\nnot a footer" >msg &&
	sha1=$(but cummit-tree -p initial mesg-with-footer^{tree} <msg) &&
	but cherry-pick -x $sha1 &&
	but log -1 --pretty=format:%B >actual &&

	printf "\n\n(cherry picked from cummit %s)\n" $sha1 >>msg &&
	test_cmp msg actual
'

test_expect_success 'cherry-pick -s handles cummits with no NL at end of message' '
	pristine_detach initial &&
	printf "title\n\nSigned-off-by: A <a@example.com>" >msg &&
	sha1=$(but cummit-tree -p initial mesg-with-footer^{tree} <msg) &&
	but cherry-pick -s $sha1 &&
	but log -1 --pretty=format:%B >actual &&

	printf "\nSigned-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>\n" >>msg &&
	test_cmp msg actual
'

test_expect_success 'cherry-pick -s handles cummits with no footer and no NL at end of message' '
	pristine_detach initial &&
	printf "title\n\nnot a footer" >msg &&
	sha1=$(but cummit-tree -p initial mesg-with-footer^{tree} <msg) &&
	but cherry-pick -s $sha1 &&
	but log -1 --pretty=format:%B >actual &&

	printf "\n\nSigned-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>\n" >>msg &&
	test_cmp msg actual
'

test_expect_success 'cherry-pick -x treats "(cherry picked from..." line as part of footer' '
	pristine_detach initial &&
	sha1=$(but rev-parse mesg-with-cherry-footer^0) &&
	but cherry-pick -x mesg-with-cherry-footer &&
	cat <<-EOF >expect &&
		$mesg_with_cherry_footer
		(cherry picked from cummit $sha1)
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -s treats "(cherry picked from..." line as part of footer' '
	pristine_detach initial &&
	but cherry-pick -s mesg-with-cherry-footer &&
	cat <<-EOF >expect &&
		$mesg_with_cherry_footer
		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x -s treats "(cherry picked from..." line as part of footer' '
	pristine_detach initial &&
	sha1=$(but rev-parse mesg-with-cherry-footer^0) &&
	but cherry-pick -x -s mesg-with-cherry-footer &&
	cat <<-EOF >expect &&
		$mesg_with_cherry_footer
		(cherry picked from cummit $sha1)
		Signed-off-by: $GIT_CUMMITTER_NAME <$GIT_CUMMITTER_EMAIL>
	EOF
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick preserves cummit message' '
	pristine_detach initial &&
	printf "$mesg_unclean" >expect &&
	but log -1 --pretty=format:%B mesg-unclean >actual &&
	test_cmp expect actual &&
	but cherry-pick mesg-unclean &&
	but log -1 --pretty=format:%B >actual &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x cleans cummit message' '
	pristine_detach initial &&
	but cherry-pick -x mesg-unclean &&
	but log -1 --pretty=format:%B >actual &&
	printf "%s\n(cherry picked from cummit %s)\n" \
		"$mesg_unclean" $(but rev-parse mesg-unclean) |
			but stripspace >expect &&
	test_cmp expect actual
'

test_expect_success 'cherry-pick -x respects cummit.cleanup' '
	pristine_detach initial &&
	but -c cummit.cleanup=strip cherry-pick -x mesg-unclean &&
	but log -1 --pretty=format:%B >actual &&
	printf "%s\n(cherry picked from cummit %s)\n" \
		"$mesg_unclean" $(but rev-parse mesg-unclean) |
			but stripspace -s >expect &&
	test_cmp expect actual
'

test_done
