#!/bin/sh

# The upstream repository (bare_repo.git) contains the configurations:
#
#	[transfer] hiderefs = hook:
#
# During the reference advertise phase the hide-refs hook will be invoked and all the refs will be checked by it

# Git client can not fetch the refs that are hidden by the hide-refs hook
test_expect_success "$PROTOCOL (protocol: $GIT_TEST_PROTOCOL_VERSION): mirror clone while hide-refs hide part of refs" '
	rm -rf local.git &&
	write_script "$BAREREPO_GIT_DIR/hooks/hide-refs" <<-EOF &&
	test-tool hide-refs \
		-H "HEAD" \
		-H "refs/heads/dev" \
		-H "refs/heads/main"
	EOF
	git -c protocol.version=$GIT_TEST_PROTOCOL_VERSION clone --mirror "$BAREREPO_URL" local.git &&
	git -C local.git show-ref -d >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
		<COMMIT-C> refs/pull-requests/1/head
		<COMMIT-TAG-v123> refs/tags/v123
		<COMMIT-D> refs/tags/v123^{}
	EOF
	test_cmp expect actual
'

# If a ref is hidden by the hide-refs hook, its private commits (tip or non-tip) will be forced hidden
# to the client, and the client can not fetch such kind of commits even if the server set allowTipSHA1InWant
# or allowReachableSHA1InWant to true
test_expect_success "$PROTOCOL (protocol: $GIT_TEST_PROTOCOL_VERSION): fetch a commit which is hided by hide-refs hook" '
	rm -rf local.git &&
	write_script "$BAREREPO_GIT_DIR/hooks/hide-refs" <<-EOF &&
	test-tool hide-refs \
		-H "refs/heads/dev" \
		-H "refs/pull-requests/1/head" \
		-H "refs/tags/v123"
	EOF
	git -C "$BAREREPO_GIT_DIR" config uploadpack.allowTipSHA1InWant true &&
	git -C "$BAREREPO_GIT_DIR" config uploadpack.allowReachableSHA1InWant true &&
	git init local.git &&
	git -C local.git remote add origin "$BAREREPO_URL" &&
	test_must_fail git -C local.git -c protocol.version=$GIT_TEST_PROTOCOL_VERSION fetch "$BAREREPO_URL" $B
'
