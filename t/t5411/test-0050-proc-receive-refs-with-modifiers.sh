test_expect_success "config receive.procReceiveRefs with modifiers ($PROTOCOL)" '
	(
		cd "$upstream" &&
		but config --unset-all receive.procReceiveRefs &&
		but config --add receive.procReceiveRefs m:refs/heads/main &&
		but config --add receive.procReceiveRefs ad:refs/heads &&
		but config --add receive.procReceiveRefs "a!:refs/heads"
	)
'

test_expect_success "setup proc-receive hook ($PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/heads/main" \
		-r "option refname refs/pull/123/head" \
		-r "option old-oid $A" \
		-r "option new-oid $B" \
		-r "ok refs/tags/v123 " \
		-r "option refname refs/pull/124/head"
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
# but push         : main(B)  tags/v123
test_expect_success "proc-receive: update branch and new tag ($PROTOCOL)" '
	but -C workbench push origin \
		$B:refs/heads/main \
		v123 >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> remote: pre-receive< <ZERO-OID> <TAG-v123> refs/tags/v123        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/main        Z
	> remote: proc-receive< <ZERO-OID> <TAG-v123> refs/tags/v123        Z
	> remote: proc-receive> ok refs/heads/main        Z
	> remote: proc-receive> option refname refs/pull/123/head        Z
	> remote: proc-receive> option old-oid <CUMMIT-A>        Z
	> remote: proc-receive> option new-oid <CUMMIT-B>        Z
	> remote: proc-receive> ok refs/tags/v123         Z
	> remote: proc-receive> option refname refs/pull/124/head        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/pull/123/head        Z
	> remote: post-receive< <ZERO-OID> <TAG-v123> refs/pull/124/head        Z
	> To <URL/of/upstream.but>
	>    <CUMMIT-A>..<CUMMIT-B>  <CUMMIT-B> -> refs/pull/123/head
	>  * [new reference]   v123 -> refs/pull/124/head
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-A> refs/heads/main
	EOF
'

# Refs of upstream : main(A)
# Refs of workbench: main(A)  tags/v123
test_expect_success "setup upstream: create tags/v123 ($PROTOCOL)" '
	but -C "$upstream" update-ref refs/heads/topic $A &&
	but -C "$upstream" update-ref refs/tags/v123 $TAG &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-A> refs/heads/main
	<CUMMIT-A> refs/heads/topic
	<TAG-v123> refs/tags/v123
	EOF
'

test_expect_success "setup proc-receive hook ($PROTOCOL)" '
	test_hook -C "$upstream" --clobber proc-receive <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/heads/main" \
		-r "option refname refs/pull/123/head" \
		-r "option old-oid $A" \
		-r "option new-oid $ZERO_OID" \
		-r "ok refs/heads/next" \
		-r "option refname refs/pull/124/head" \
		-r "option new-oid $A"
	EOF
'

# Refs of upstream : main(A)  topic(A)  tags/v123
# Refs of workbench: main(A)            tags/v123
# but push         : NULL       topic(B)  NULL       next(A)
test_expect_success "proc-receive: create/delete branch, and delete tag ($PROTOCOL)" '
	but -C workbench push origin \
		:refs/heads/main \
		$B:refs/heads/topic \
		$A:refs/heads/next \
		:refs/tags/v123 >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	format_and_save_expect <<-EOF &&
	> remote: # pre-receive hook        Z
	> remote: pre-receive< <CUMMIT-A> <ZERO-OID> refs/heads/main        Z
	> remote: pre-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/topic        Z
	> remote: pre-receive< <TAG-v123> <ZERO-OID> refs/tags/v123        Z
	> remote: pre-receive< <ZERO-OID> <CUMMIT-A> refs/heads/next        Z
	> remote: # proc-receive hook        Z
	> remote: proc-receive< <CUMMIT-A> <ZERO-OID> refs/heads/main        Z
	> remote: proc-receive< <ZERO-OID> <CUMMIT-A> refs/heads/next        Z
	> remote: proc-receive> ok refs/heads/main        Z
	> remote: proc-receive> option refname refs/pull/123/head        Z
	> remote: proc-receive> option old-oid <CUMMIT-A>        Z
	> remote: proc-receive> option new-oid <ZERO-OID>        Z
	> remote: proc-receive> ok refs/heads/next        Z
	> remote: proc-receive> option refname refs/pull/124/head        Z
	> remote: proc-receive> option new-oid <CUMMIT-A>        Z
	> remote: # post-receive hook        Z
	> remote: post-receive< <CUMMIT-A> <ZERO-OID> refs/pull/123/head        Z
	> remote: post-receive< <CUMMIT-A> <CUMMIT-B> refs/heads/topic        Z
	> remote: post-receive< <TAG-v123> <ZERO-OID> refs/tags/v123        Z
	> remote: post-receive< <ZERO-OID> <CUMMIT-A> refs/pull/124/head        Z
	> To <URL/of/upstream.but>
	>  - [deleted]         refs/pull/123/head
	>    <CUMMIT-A>..<CUMMIT-B>  <CUMMIT-B> -> topic
	>  - [deleted]         v123
	>  * [new reference]   <CUMMIT-A> -> refs/pull/124/head
	EOF
	test_cmp expect actual &&

	test_cmp_refs -C "$upstream" <<-EOF
	<CUMMIT-A> refs/heads/main
	<CUMMIT-B> refs/heads/topic
	EOF
'
