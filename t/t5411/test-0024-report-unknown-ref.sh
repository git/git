test_expect_success "setup proc-receive hook (unexpected ref, $PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/a/b/c/my/topic
test_expect_success "proc-receive: report unknown reference ($PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:refs/for/a/b/c/my/topic \
		>out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/a/b/c/my/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/a/b/c/my/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: error: proc-receive reported status on unknown ref: refs/for/main/topic        Z
	> To <URL/of/upstream.git>
	>  ! [remote rejected] HEAD -> refs/for/a/b/c/my/topic (proc-receive failed to report status)
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<COMMIT-A> refs/heads/main
	EOF
'
