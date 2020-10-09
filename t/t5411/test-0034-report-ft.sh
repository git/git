test_expect_success "setup proc-receive hook (ft, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/master/topic" \
		-r "option fall-through"
	EOF
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push         :                       refs/for/master/topic(B)
test_expect_success "proc-receive: fall throught, let receive-pack to execute ($PROTOCOL)" '
	git -C workbench push origin \
		$B:refs/for/master/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <ZERO-OID> <COMMIT-B> refs/for/master/topic
	remote: # proc-receive hook
	remote: proc-receive< <ZERO-OID> <COMMIT-B> refs/for/master/topic
	remote: proc-receive> ok refs/for/master/topic
	remote: proc-receive> option fall-through
	remote: # post-receive hook
	remote: post-receive< <ZERO-OID> <COMMIT-B> refs/for/master/topic
	To <URL/of/upstream.git>
	 * [new reference] <COMMIT-B> -> refs/for/master/topic
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/for/master/topic
	<COMMIT-A> refs/heads/master
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(A)             refs/for/master/topic(A)
# Refs of workbench: master(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	git -C "$upstream" update-ref -d refs/for/master/topic
'
