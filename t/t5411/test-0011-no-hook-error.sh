# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push         :                       next(A)  refs/for/master/topic(A)
test_expect_success "proc-receive: no hook, fail to push special ref ($PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:next \
		HEAD:refs/for/master/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	remote: error: cannot find hook "proc-receive"
	remote: # post-receive hook
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	To <URL/of/upstream.git>
	 * [new branch] HEAD -> next
	 ! [remote rejected] HEAD -> refs/for/master/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/master
	<COMMIT-A> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(A)             next(A)
# Refs of workbench: master(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	git -C "$upstream" update-ref -d refs/heads/next
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push --atomic: (B)                   next(A)  refs/for/master/topic(A)
test_expect_success "proc-receive: no hook, all failed for atomic push ($PROTOCOL)" '
	test_must_fail git -C workbench push --atomic origin \
		$B:master \
		HEAD:next \
		HEAD:refs/for/master/topic >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	remote: error: cannot find hook "proc-receive"
	To <URL/of/upstream.git>
	 ! [remote rejected] <COMMIT-B> -> master (fail to run proc-receive hook)
	 ! [remote rejected] HEAD -> next (fail to run proc-receive hook)
	 ! [remote rejected] HEAD -> refs/for/master/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/master
	EOF
	test_cmp expect actual
'
