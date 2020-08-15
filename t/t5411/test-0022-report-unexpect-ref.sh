test_expect_success "setup proc-receive hook (unexpected ref, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/heads/master"
	EOF
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push         : (B)                   refs/for/master/topic
test_expect_success "proc-receive: report unexpected ref ($PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		$B:refs/heads/master \
		HEAD:refs/for/master/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	remote: # proc-receive hook
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	remote: proc-receive> ok refs/heads/master
	remote: error: proc-receive reported status on unexpected ref: refs/heads/master
	remote: # post-receive hook
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	To <URL/of/upstream.git>
	 <OID-A>..<OID-B> <COMMIT-B> -> master
	 ! [remote rejected] HEAD -> refs/for/master/topic (proc-receive failed to report status)
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/heads/master
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(B)
# Refs of workbench: master(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	git -C "$upstream" update-ref refs/heads/master $A
'
