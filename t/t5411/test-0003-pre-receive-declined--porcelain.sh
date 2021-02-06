test_expect_success "setup pre-receive hook ($PROTOCOL/porcelain)" '
	mv "$upstream/hooks/pre-receive" "$upstream/hooks/pre-receive.ok" &&
	write_script "$upstream/hooks/pre-receive" <<-EOF
	exit 1
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git-push         : main(B)             next(A)
test_expect_success "git-push is declined ($PROTOCOL/porcelain)" '
	test_must_fail git -C workbench push --porcelain origin \
		$B:refs/heads/main \
		HEAD:refs/heads/next \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	!    <COMMIT-B>:refs/heads/main    [remote rejected] (pre-receive hook declined)
	!    HEAD:refs/heads/next    [remote rejected] (pre-receive hook declined)
	Done
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

test_expect_success "cleanup ($PROTOCOL/porcelain)" '
	mv "$upstream/hooks/pre-receive.ok" "$upstream/hooks/pre-receive"
'
