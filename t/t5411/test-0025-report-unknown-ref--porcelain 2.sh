test_expect_success "setup proc-receive hook (unexpected ref, $PROTOCOL/porcelain)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/master/topic"
	EOF
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push         :                       refs/for/a/b/c/my/topic
test_expect_success "proc-receive: report unknown reference ($PROTOCOL/porcelain)" '
	test_must_fail git -C workbench push --porcelain origin \
		HEAD:refs/for/a/b/c/my/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/a/b/c/my/topic
	remote: # proc-receive hook
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/a/b/c/my/topic
	remote: proc-receive> ok refs/for/master/topic
	remote: error: proc-receive reported status on unknown ref: refs/for/master/topic
	To <URL/of/upstream.git>
	!    HEAD:refs/for/a/b/c/my/topic    [remote rejected] (proc-receive failed to report status)
	Done
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/master
	EOF
	test_cmp expect actual
'
