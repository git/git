# Refs of upstream : main(A)  
# Refs of workbench: main(A)  tags/v123
# git-push         : main(B)             next(A)
test_expect_success "git-push ($PROTOCOL)" '
	git -C workbench push origin \
		$B:refs/heads/main \
		HEAD:refs/heads/next \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/main
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: # post-receive hook
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/heads/main
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	To <URL/of/upstream.git>
	 <OID-A>..<OID-B> <COMMIT-B> -> main
	 * [new branch] HEAD -> next
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/heads/main
	<COMMIT-A> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : main(B)  next(A)
# Refs of workbench: main(A)           tags/v123
# git-push --atomic: main(A)  next(B)
test_expect_success "git-push --atomic ($PROTOCOL)" '
	test_must_fail git -C workbench push --atomic origin \
		main \
		$B:refs/heads/next \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out |
		sed -n \
			-e "/^To / { s/   */ /g; p; }" \
			-e "/^ ! / { s/   */ /g; p; }" \
			>actual &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	 ! [rejected] main -> main (non-fast-forward)
	 ! [rejected] <COMMIT-B> -> next (atomic push failed)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/heads/main
	<COMMIT-A> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : main(B)  next(A)
# Refs of workbench: main(A)           tags/v123
# git-push         : main(A)  next(B)
test_expect_success "non-fast-forward git-push ($PROTOCOL)" '
	test_must_fail git \
		-C workbench \
		-c advice.pushUpdateRejected=false \
		push origin \
		main \
		$B:refs/heads/next \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/next
	remote: # post-receive hook
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/heads/next
	To <URL/of/upstream.git>
	 <OID-A>..<OID-B> <COMMIT-B> -> next
	 ! [rejected] main -> main (non-fast-forward)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/heads/main
	<COMMIT-B> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : main(B)  next(B)
# Refs of workbench: main(A)           tags/v123
# git-push -f      : main(A)  NULL     tags/v123  refs/review/main/topic(A)  a/b/c(A)
test_expect_success "git-push -f ($PROTOCOL)" '
	git -C workbench push -f origin \
		refs/tags/v123 \
		:refs/heads/next \
		main \
		main:refs/review/main/topic \
		HEAD:refs/heads/a/b/c \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-B> <COMMIT-A> refs/heads/main
	remote: pre-receive< <COMMIT-B> <ZERO-OID> refs/heads/next
	remote: pre-receive< <ZERO-OID> <TAG-v123> refs/tags/v123
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/review/main/topic
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/a/b/c
	remote: # post-receive hook
	remote: post-receive< <COMMIT-B> <COMMIT-A> refs/heads/main
	remote: post-receive< <COMMIT-B> <ZERO-OID> refs/heads/next
	remote: post-receive< <ZERO-OID> <TAG-v123> refs/tags/v123
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/review/main/topic
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/a/b/c
	To <URL/of/upstream.git>
	 + <OID-B>...<OID-A> main -> main (forced update)
	 - [deleted] next
	 * [new tag] v123 -> v123
	 * [new reference] main -> refs/review/main/topic
	 * [new branch] HEAD -> a/b/c
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/a/b/c
	<COMMIT-A> refs/heads/main
	<COMMIT-A> refs/review/main/topic
	<TAG-v123> refs/tags/v123
	EOF
	test_cmp expect actual
'

# Refs of upstream : main(A)  tags/v123  refs/review/main/topic(A)  a/b/c(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	(
		cd "$upstream" &&
		git update-ref -d refs/review/main/topic &&
		git update-ref -d refs/tags/v123 &&
		git update-ref -d refs/heads/a/b/c
	)
'
