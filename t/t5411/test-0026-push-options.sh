test_expect_success "setup proc-receive hook and disable push-options ($PROTOCOL)" '
	git -C "$upstream" config receive.advertisePushOptions false &&
	write_script "$upstream/hooks/proc-receive" <<-EOF
	printf >&2 "# proc-receive hook\n"
	test-tool proc-receive -v \
		-r "ok refs/for/master/topic"
	EOF
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push -o ...  :                       refs/for/master/topic
test_expect_success "proc-receive: not support push options ($PROTOCOL)" '
	test_must_fail git -C workbench push \
		-o issue=123 \
		-o reviewer=user1 \
		origin \
		HEAD:refs/for/master/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	test_i18ngrep "fatal: the receiving end does not support push options" \
		actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/master
	EOF
	test_cmp expect actual
'

test_expect_success "enable push options ($PROTOCOL)" '
	git -C "$upstream" config receive.advertisePushOptions true
'

# Refs of upstream : master(A)
# Refs of workbench: master(A)  tags/v123
# git push -o ...  :                       next(A)  refs/for/master/topic
test_expect_success "proc-receive: push with options ($PROTOCOL)" '
	git -C workbench push \
		--atomic \
		-o issue=123 \
		-o reviewer=user1 \
		origin \
		HEAD:refs/heads/next \
		HEAD:refs/for/master/topic \
		>out 2>&1 &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	remote: # pre-receive hook
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: pre-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	remote: # proc-receive hook
	remote: proc-receive: atomic push_options
	remote: proc-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	remote: proc-receive< issue=123
	remote: proc-receive< reviewer=user1
	remote: proc-receive> ok refs/for/master/topic
	remote: # post-receive hook
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/heads/next
	remote: post-receive< <ZERO-OID> <COMMIT-A> refs/for/master/topic
	To <URL/of/upstream.git>
	 * [new branch] HEAD -> next
	 * [new reference] HEAD -> refs/for/master/topic
	EOF
	test_cmp expect actual &&
	git -C "$upstream" show-ref >out &&
	make_user_friendly_and_stable_output <out >actual &&
	cat >expect <<-EOF &&
	<COMMIT-A> refs/heads/master
	<COMMIT-A> refs/heads/next
	EOF
	test_cmp expect actual
'

# Refs of upstream : master(A)             next(A)
# Refs of workbench: master(A)  tags/v123
test_expect_success "cleanup ($PROTOCOL)" '
	git -C "$upstream" update-ref -d refs/heads/next
'
