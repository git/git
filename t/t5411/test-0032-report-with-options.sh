test_expect_success "setup proc-receive hook (option without matching ok, $PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "option refname refs/pull/123/head" \
		-r "option old-oid $B"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/next/topic(A)  refs/for/a/b/c/topic(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: report option without matching ok ($PROTOCOL)" '
	test_must_fail git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out-$test_count 2>&1 &&
	make_user_friendly_and_stable_output <out-$test_count >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/pull/123/head        Z
	> remote: proc-receive> option old-oid <cummit-B>        Z
	> remote: error: proc-receive reported "option" without a matching "ok/ng" directive        Z
	> To <URL/of/upstream.git>
	>  ! [remote rejected] HEAD -> refs/for/main/topic (proc-receive failed to report status)
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (option refname, $PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-\EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/pull/123/head"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/next/topic(A)  refs/for/a/b/c/topic(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: report option refname ($PROTOCOL)" '
	git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/pull/123/head        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <cummit-A> refs/pull/123/head        Z
	> To <URL/of/upstream.git>
	>  * [new reference]   HEAD -> refs/pull/123/head
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (option refname and forced-update, $PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-\EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/pull/123/head" \
		-r "option forced-update"
	EOF
'
# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/next/topic(A)  refs/for/a/b/c/topic(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: report option refname and forced-update ($PROTOCOL)" '
	git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/pull/123/head        Z
	> remote: proc-receive> option forced-update        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <cummit-A> refs/pull/123/head        Z
	> To <URL/of/upstream.git>
	>  * [new reference]   HEAD -> refs/pull/123/head
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (option refname and old-oid, $PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/pull/123/head" \
		-r "option old-oid $B"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/next/topic(A)  refs/for/a/b/c/topic(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: report option refname and old-oid ($PROTOCOL)" '
	git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/pull/123/head        Z
	> remote: proc-receive> option old-oid <cummit-B>        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <cummit-B> <cummit-A> refs/pull/123/head        Z
	> To <URL/of/upstream.git>
	>    <cummit-B>..<cummit-A>  HEAD -> refs/pull/123/head
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (option old-oid, $PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option old-oid $B"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/next/topic(A)  refs/for/a/b/c/topic(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: report option old-oid ($PROTOCOL)" '
	git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option old-oid <cummit-B>        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <cummit-B> <cummit-A> refs/for/main/topic        Z
	> To <URL/of/upstream.git>
	>    <cummit-B>..<cummit-A>  HEAD -> refs/for/main/topic
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (option old-oid and new-oid, $PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option old-oid $A" \
		-r "option new-oid $B"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/next/topic(A)  refs/for/a/b/c/topic(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: report option old-oid and new-oid ($PROTOCOL)" '
	git -C workbench push origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option old-oid <cummit-A>        Z
	> remote: proc-receive> option new-oid <cummit-B>        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <cummit-A> <cummit-B> refs/for/main/topic        Z
	> To <URL/of/upstream.git>
	>    <cummit-A>..<cummit-B>  HEAD -> refs/for/main/topic
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook (report with multiple rewrites, $PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/a/b/c/topic" \
		-r "ok refs/for/next/topic" \
		-r "option refname refs/pull/123/head" \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/pull/124/head" \
		-r "option old-oid $B" \
		-r "option forced-update" \
		-r "option new-oid $A"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/next/topic(A)  refs/for/a/b/c/topic(A)  refs/for/main/topic(A)
test_expect_success "proc-receive: report with multiple rewrites ($PROTOCOL)" '
	git -C workbench push origin \
		HEAD:refs/for/next/topic \
		HEAD:refs/for/a/b/c/topic \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/next/topic        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/a/b/c/topic        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/next/topic        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/a/b/c/topic        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/a/b/c/topic        Z
	> remote: proc-receive> ok refs/for/next/topic        Z
	> remote: proc-receive> option refname refs/pull/123/head        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/pull/124/head        Z
	> remote: proc-receive> option old-oid <cummit-B>        Z
	> remote: proc-receive> option forced-update        Z
	> remote: proc-receive> option new-oid <cummit-A>        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <cummit-A> refs/pull/123/head        Z
	> remote: post-receive< <ZERO-OID> <cummit-A> refs/for/a/b/c/topic        Z
	> remote: post-receive< <cummit-B> <cummit-A> refs/pull/124/head        Z
	> To <URL/of/upstream.git>
	>  * [new reference]   HEAD -> refs/pull/123/head
	>  * [new reference]   HEAD -> refs/for/a/b/c/topic
	>  + <cummit-B>...<cummit-A> HEAD -> refs/pull/124/head (forced update)
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<cummit-A> refs/heads/main
	EOF
'
