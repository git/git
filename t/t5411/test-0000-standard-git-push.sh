# Refs of upstream : master(A)  
# Refs of workbench: master(A)  tags/v123
# git-push         : master(B)             next(A)
test_expect_success "git-push ($PROTOCOL)" '
	git -C workbench push origin \
		$B:refs/heads/master \
		HEAD:refs/heads/next \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: # post-receive hook
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	To <URL/of/upstream.git>
	 <OID-A>..<OID-B> <COMMIT-B> -> master
	 * [new branch] HEAD -> next
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/heads/master
	<COMMIT-A> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(B)  next(A)
# Refs of workbench: master(A)           tags/v123
# git-push --atomic: master(A)  next(B)
test_expect_success "git-push --atomic ($PROTOCOL)" '
	test_must_fail git -C workbench push --atomic origin \
		master \
		$B:refs/heads/next \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out |
		sed -n \
			-e "/^To / { s/   */ /g; p; }" \
			-e "/^ ! / { s/   */ /g; p; }" \
			>actual &&
	cat >expect <<-EOF &&
	To <URL/of/upstream.git>
	 ! [rejected] master -> master (non-fast-forward)
	 ! [rejected] <COMMIT-B> -> next (atomic push failed)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/heads/master
	<COMMIT-A> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(B)  next(A)
# Refs of workbench: master(A)           tags/v123
# git-push         : master(A)  next(B)
test_expect_success "non-fast-forward git-push ($PROTOCOL)" '
	test_must_fail git \
		-C workbench \
		-c advice.pushUpdateRejected=false \
		push origin \
		master \
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
	 ! [rejected] master -> master (non-fast-forward)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/heads/master
	<COMMIT-B> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(B)  next(B)
# Refs of workbench: master(A)           tags/v123
# git-push -f      : master(A)  NULL     tags/v123  refs/review/master/topic(A)  a/b/c(A)
test_expect_success "git-push -f ($PROTOCOL)" '
	git -C workbench push -f origin \
		refs/tags/v123 \
		:refs/heads/next \
		master \
		master:refs/review/master/topic \
		HEAD:refs/heads/a/b/c \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-B> <COMMIT-A> refs/heads/master
	remote: pre-receive< <COMMIT-B> <ZERO-OID> refs/heads/next
	remote: pre-receive< <ZERO-OID> <TAG-v123> refs/tags/v123
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/review/master/topic
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/a/b/c
	remote: # post-receive hook
	remote: post-receive< <COMMIT-B> <COMMIT-A> refs/heads/master
	remote: post-receive< <COMMIT-B> <ZERO-OID> refs/heads/next
	remote: post-receive< <ZERO-OID> <TAG-v123> refs/tags/v123
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/review/master/topic
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/a/b/c
	To <URL/of/upstream.git>
	 + <OID-B>...<OID-A> master -> master (forced update)
	 - [deleted] next
	 * [new tag] v123 -> v123
	 * [new reference] master -> refs/review/master/topic
	 * [new branch] HEAD -> a/b/c
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/a/b/c
	<COMMIT-A> refs/heads/master
	<COMMIT-A> refs/review/master/topic
	<TAG-v123> refs/tags/v123
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(A)  tags/v123  refs/review/master/topic(A)  a/b/c(A)
# Refs of workbench: master(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	(
		cd "$upstream" &&
		git update-ref -d refs/review/master/topic &&
		git update-ref -d refs/tags/v123 &&
		git update-ref -d refs/heads/a/b/c
	)
'
