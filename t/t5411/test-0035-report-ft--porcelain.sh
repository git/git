test_expect_success "setup proc-receive hook (fall-through, $PROTOCOL/porcelain)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option fall-through"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(B)
test_expect_success "proc-receive: fall throught, let receive-pack to execute ($PROTOCOL/porcelain)" '
	git -C workbench push --porcelain origin \
		$B:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <ZERO-OID> <COMMIT-B> refs/for/main/topic
	remote: # proc-receive hook
	remote: proc-receive< <ZERO-OID> <COMMIT-B> refs/for/main/topic
	remote: proc-receive> ok refs/for/main/topic
	remote: proc-receive> option fall-through
	remote: # post-receive hook
	remote: post-receive< <ZERO-OID> <COMMIT-B> refs/for/main/topic
	To <URL/of/upstream.git>
	*    <COMMIT-B>:refs/for/main/topic    [new reference]
	Done
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-B> refs/for/main/topic
	<COMMIT-A> refs/heads/main
	EOF
	test_cmp expect actual
'

# Refs of upstream : main(A)             refs/for/main/topic(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL/porcelain)" '
	git -C "$upstream" update-ref -d refs/for/main/topic
'
