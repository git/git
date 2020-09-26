test_expect_success "config receive.procReceiveRefs with modifiers ($PROTOCOL)" '
	(
		cd "$upstream" &&
		git config --unset-all receive.procReceiveRefs &&
		git config --add receive.procReceiveRefs m:refs/heads/master &&
		git config --add receive.procReceiveRefs ad:refs/heads &&
		git config --add receive.procReceiveRefs "a!:refs/heads"
	)
'

test_expect_success "setup proc-receive hook ($PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/heads/master" \
		-r "option refname refs/pull/123/head" \
		-r "option old-oid $A" \
		-r "option new-oid $B" \
		-r "ok refs/tags/v123 " \
		-r "option refname refs/pull/124/head"
	EOF
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push         : master(B)  tags/v123
test_expect_success "proc-receive: update branch and new tag ($PROTOCOL)" '
	git -C workbench push origin \
		$B:refs/heads/master \
		v123 >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	remote: pre-receive< <ZERO-OID> <TAG-v123> refs/tags/v123
	remote: # proc-receive hook
	remote: proc-receive< <COMMIT-A> <COMMIT-B> refs/heads/master
	remote: proc-receive< <ZERO-OID> <TAG-v123> refs/tags/v123
	remote: proc-receive> ok refs/heads/master
	remote: proc-receive> option refname refs/pull/123/head
	remote: proc-receive> option old-oid <COMMIT-A>
	remote: proc-receive> option new-oid <COMMIT-B>
	remote: proc-receive> ok refs/tags/v123
	remote: proc-receive> option refname refs/pull/124/head
	remote: # post-receive hook
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/pull/123/head
	remote: post-receive< <ZERO-OID> <TAG-v123> refs/pull/124/head
	To <URL/of/upstream.git>
	 <OID-A>..<OID-B> <COMMIT-B> -> refs/pull/123/head
	 * [new reference] v123 -> refs/pull/124/head
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/master
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
test_expect_success "setup upstream: create tags/v123 ($PROTOCOL)" '
	git -C "$upstream" update-ref refs/heads/topic $A &&
	git -C "$upstream" update-ref refs/tags/v123 $TAG &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/master
	<COMMIT-A> refs/heads/topic
	<TAG-v123> refs/tags/v123
	EOF
	test_cmp expect actual
'

test_expect_success "setup proc-receive hook ($PROTOCOL)" '
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/heads/master" \
		-r "option refname refs/pull/123/head" \
		-r "option old-oid $A" \
		-r "option new-oid $ZERO_OID" \
		-r "ok refs/heads/next" \
		-r "option refname refs/pull/124/head" \
		-r "option new-oid $A"
	EOF
'

# Refs of upstream : master(A)  topic(A)  tags/v123
# Refs of workbench: master(A)            tags/v123
# git push         : NULL       topic(B)  NULL       next(A)
test_expect_success "proc-receive: create/delete branch, and delete tag ($PROTOCOL)" '
	git -C workbench push origin \
		:refs/heads/master \
		$B:refs/heads/topic \
		$A:refs/heads/next \
		:refs/tags/v123 >out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <COMMIT-A> <ZERO-OID> refs/heads/master
	remote: pre-receive< <COMMIT-A> <COMMIT-B> refs/heads/topic
	remote: pre-receive< <TAG-v123> <ZERO-OID> refs/tags/v123
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: # proc-receive hook
	remote: proc-receive< <COMMIT-A> <ZERO-OID> refs/heads/master
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: proc-receive> ok refs/heads/master
	remote: proc-receive> option refname refs/pull/123/head
	remote: proc-receive> option old-oid <COMMIT-A>
	remote: proc-receive> option new-oid <ZERO-OID>
	remote: proc-receive> ok refs/heads/next
	remote: proc-receive> option refname refs/pull/124/head
	remote: proc-receive> option new-oid <COMMIT-A>
	remote: # post-receive hook
	remote: post-receive< <COMMIT-A> <ZERO-OID> refs/pull/123/head
	remote: post-receive< <COMMIT-A> <COMMIT-B> refs/heads/topic
	remote: post-receive< <TAG-v123> <ZERO-OID> refs/tags/v123
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/pull/124/head
	To <URL/of/upstream.git>
	 - [deleted] refs/pull/123/head
	 <OID-A>..<OID-B> <COMMIT-B> -> topic
	 - [deleted] v123
	 * [new reference] <COMMIT-A> -> refs/pull/124/head
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/master
	<COMMIT-B> refs/heads/topic
	EOF
	test_cmp expect actual
'
