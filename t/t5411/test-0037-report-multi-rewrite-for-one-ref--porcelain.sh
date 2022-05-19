test_expect_success "setup proc-receive hook (multiple rewrites for one ref, no refname for the 1st rewrite, $PROTOCOL/porcelain)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option old-oid $A" \
		-r "option new-oid $B" \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/changes/24/124/1" \
		-r "option old-oid $ZERO_OID" \
		-r "option new-oid $A" \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/changes/25/125/1" \
		-r "option old-oid $A" \
		-r "option new-oid $B"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: multiple rewrite for one ref, no refname for the 1st rewrite ($PROTOCOL/porcelain)" '
	git -C workbench push --porcelain origin \
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
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/changes/24/124/1        Z
	> remote: proc-receive> option old-oid <ZERO-OID>        Z
	> remote: proc-receive> option new-oid <cummit-A>        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/changes/25/125/1        Z
	> remote: proc-receive> option old-oid <cummit-A>        Z
	> remote: proc-receive> option new-oid <cummit-B>        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <cummit-A> <cummit-B> refs/for/main/topic        Z
	> remote: post-receive< <ZERO-OID> <cummit-A> refs/changes/24/124/1        Z
	> remote: post-receive< <cummit-A> <cummit-B> refs/changes/25/125/1        Z
	> To <URL/of/upstream.git>
	>  	HEAD:refs/for/main/topic	<cummit-A>..<cummit-B>
	> *	HEAD:refs/changes/24/124/1	[new reference]
	>  	HEAD:refs/changes/25/125/1	<cummit-A>..<cummit-B>
	> Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<cummit-A> refs/heads/main
	EOF
'

test_expect_success "setup proc-receive hook (multiple rewrites for one ref, no refname for the 2nd rewrite, $PROTOCOL/porcelain)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/changes/24/124/1" \
		-r "option old-oid $ZERO_OID" \
		-r "option new-oid $A" \
		-r "ok refs/for/main/topic" \
		-r "option old-oid $A" \
		-r "option new-oid $B" \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/changes/25/125/1" \
		-r "option old-oid $B" \
		-r "option new-oid $A" \
		-r "option forced-update"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: multiple rewrites for one ref, no refname for the 2nd rewrite ($PROTOCOL/porcelain)" '
	git -C workbench push --porcelain origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/changes/24/124/1        Z
	> remote: proc-receive> option old-oid <ZERO-OID>        Z
	> remote: proc-receive> option new-oid <cummit-A>        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option old-oid <cummit-A>        Z
	> remote: proc-receive> option new-oid <cummit-B>        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/changes/25/125/1        Z
	> remote: proc-receive> option old-oid <cummit-B>        Z
	> remote: proc-receive> option new-oid <cummit-A>        Z
	> remote: proc-receive> option forced-update        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <cummit-A> refs/changes/24/124/1        Z
	> remote: post-receive< <cummit-A> <cummit-B> refs/for/main/topic        Z
	> remote: post-receive< <cummit-B> <cummit-A> refs/changes/25/125/1        Z
	> To <URL/of/upstream.git>
	> *	HEAD:refs/changes/24/124/1	[new reference]
	>  	HEAD:refs/for/main/topic	<cummit-A>..<cummit-B>
	> +	HEAD:refs/changes/25/125/1	<cummit-B>...<cummit-A> (forced update)
	> Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<cummit-A> refs/heads/main
	EOF
'

test_expect_success "setup proc-receive hook (multiple rewrites for one ref, $PROTOCOL/porcelain)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/changes/23/123/1" \
		-r "ok refs/for/main/topic" \
		-r "option refname refs/changes/24/124/2" \
		-r "option old-oid $A" \
		-r "option new-oid $B"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# git push         :                       refs/for/main/topic(A)
test_expect_success "proc-receive: multiple rewrites for one ref ($PROTOCOL/porcelain)" '
	git -C workbench push --porcelain origin \
		HEAD:refs/for/main/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <ZERO-OID> <cummit-A> refs/for/main/topic        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/changes/23/123/1        Z
	> remote: proc-receive> ok refs/for/main/topic        Z
	> remote: proc-receive> option refname refs/changes/24/124/2        Z
	> remote: proc-receive> option old-oid <cummit-A>        Z
	> remote: proc-receive> option new-oid <cummit-B>        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <ZERO-OID> <cummit-A> refs/changes/23/123/1        Z
	> remote: post-receive< <cummit-A> <cummit-B> refs/changes/24/124/2        Z
	> To <URL/of/upstream.git>
	> *	HEAD:refs/changes/23/123/1	[new reference]
	>  	HEAD:refs/changes/24/124/2	<cummit-A>..<cummit-B>
	> Done
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<cummit-A> refs/heads/main
	EOF
'
