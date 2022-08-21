# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       next(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: no hook, fail to push special ref ($PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:next \
		HEAD:refs/for/main/topic \
		>out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> remote: error: cannot find hook "proc-receive"        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/next        Z
	> To <URL/of/upstream.git>
	>  * [new branch]      HEAD -> next
	>  ! [remote rejected] HEAD -> refs/for/main/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<COMMIT-A> refs/heads/main
	<COMMIT-A> refs/heads/next
	EOF
'

# Refs of upstream : main(A)             next(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	git -C "$upstream" update-ref -d refs/heads/next
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push --atomic: (B)                   next(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: no hook, all failed for atomic push ($PROTOCOL)" '
	test_must_fail git -C workbench push --atomic origin \
		$B:main \
		HEAD:next \
		HEAD:refs/for/main/topic >out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/main        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> remote: error: cannot find hook "proc-receive"        Z
	> To <URL/of/upstream.git>
	>  ! [remote rejected] <COMMIT-B> -> main (fail to run proc-receive hook)
	>  ! [remote rejected] HEAD -> next (fail to run proc-receive hook)
	>  ! [remote rejected] HEAD -> refs/for/main/topic (fail to run proc-receive hook)
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<COMMIT-A> refs/heads/main
	EOF
'
