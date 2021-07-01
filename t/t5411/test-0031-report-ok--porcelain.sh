test_expect_success "setup proc-receive hook (ok, $PROTOCOL/porcelain)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic
test_expect_success "proc-receive: ok ($PROTOCOL/porcelain)" '
	git -C workbench push --porcelain origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <COMMIT-A> refs/for/main/topic        Z
	> To <URL/of/upstream.git>
	> *	HEAD:refs/for/main/topic	[new reference]
	> Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<COMMIT-A> refs/heads/main
	EOF
'
