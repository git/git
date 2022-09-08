#!/bin/sh

# The upstream repository (bare_repo.git) contains the configurations:
#
#	[transfer] hiderefs = hook:
#
# During the reference advertise phase the hide-refs hook will be invoked and all the refs will be checked by it

test_expect_success "$PROTOCOL (protocol: $GIT_TEST_PROTOCOL_VERSION): push to main while hide-refs hook does not hide it" '
	write_script "$BAREREPO_GIT_DIR/hooks/hide-refs" <<-EOF &&
		test-tool hide-refs
	EOF
	create_commits_in work_repo E &&
	git -c protocol.version=$GIT_TEST_PROTOCOL_VERSION -C work_repo push origin HEAD:main >out 2>&1 &&
	make_user_friendly_and_stable_output <out >out.tmp &&
	sed "s/$(get_abbrev_oid $E)[0-9a-f]*/<COMMIT-E>/g" <out.tmp >actual &&
	format_and_save_expect <<-EOF &&
		To <URL/of/bare_repo.git>
		   <COMMIT-A>..<COMMIT-E>  HEAD -> main
	EOF
	test_cmp expect actual
'

# If hide-refs hook hide some ref, git push will be rejected
test_expect_success "$PROTOCOL (protocol: $GIT_TEST_PROTOCOL_VERSION): push to main while hide-refs hook hide it" '
	write_script "$BAREREPO_GIT_DIR/hooks/hide-refs" <<-EOF &&
	test-tool hide-refs \
		-H "refs/heads/main"
	EOF
	create_commits_in work_repo F &&
	test_must_fail git -c protocol.version=$GIT_TEST_PROTOCOL_VERSION -C work_repo push origin HEAD:main >out 2>&1 &&
	make_user_friendly_and_stable_output <out >out.tmp &&
	sed "s/$(get_abbrev_oid $E)[0-9a-f]*/<COMMIT-E>/g" <out.tmp >actual &&
	format_and_save_expect <<-EOF &&
		To <URL/of/bare_repo.git>
		 ! [remote rejected] HEAD -> main (deny updating a hidden ref)
		error: failed to push some refs to "<URL/of/bare_repo.git>"
	EOF
	test_cmp expect actual
'
