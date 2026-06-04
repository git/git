#!/bin/sh
#
# Copyright (c) 2020 Jiang Xin
#
test_description='Test git push porcelain output'

. ./test-lib.sh

# Create commits in <repo> and assign each commit's oid to shell variables
# given in the arguments (A, B, and C). E.g.:
#
#     create_commits_in <repo> A B C
#
# NOTE: Never calling this function from a subshell since variable
# assignments will disappear when subshell exits.
create_commits_in () {
	repo="$1" && test -d "$repo" ||
	error "Repository $repo does not exist."
	shift &&
	while test $# -gt 0
	do
		name=$1 &&
		shift &&
		test_commit -C "$repo" --no-tag "$name" &&
		eval $name=$(git -C "$repo" rev-parse HEAD)
	done
}

get_abbrev_oid () {
	oid=$1 &&
	suffix=${oid#???????} &&
	oid=${oid%$suffix} &&
	if test -n "$oid"
	then
		echo "$oid"
	else
		echo "undefined-oid"
	fi
}

# Format the output of git-push, git-show-ref and other commands to make a
# user-friendly and stable text.  We can easily prepare the expect text
# without having to worry about future changes of the commit ID and spaces
# of the output.
make_user_friendly_and_stable_output () {
	sed \
		-e "s/$(get_abbrev_oid $A)[0-9a-f]*/<COMMIT-A>/g" \
		-e "s/$(get_abbrev_oid $B)[0-9a-f]*/<COMMIT-B>/g" \
		-e "s/$ZERO_OID/<ZERO-OID>/g" \
		-e "s#To $URL_PREFIX/upstream.git#To <URL/of/upstream.git>#"
}

format_and_save_expect () {
	sed -e 's/^> //' -e 's/Z$//' >expect
}

create_upstream_template () {
	git init --bare upstream-template.git &&
	git clone upstream-template.git tmp_work_dir &&
	create_commits_in tmp_work_dir A B &&
	(
		cd tmp_work_dir &&
		git push origin \
			$B:refs/heads/main \
			$A:refs/heads/foo \
			$A:refs/heads/bar \
			$A:refs/heads/baz
	) &&
	rm -rf tmp_work_dir
}

setup_upstream () {
	if test $# -ne 1
	then
		BUG "location of upstream repository is not provided"
	fi &&
	rm -rf "$1" &&
	if ! test -d upstream-template.git
	then
		create_upstream_template
	fi &&
	git clone --mirror upstream-template.git "$1" &&
	# The upstream repository provides services using the HTTP protocol.
	if ! test "$1" = "upstream.git"
	then
		git -C "$1" config http.receivepack true
	fi
}

setup_upstream_and_workbench () {
	if test $# -ne 1
	then
		BUG "location of upstream repository is not provided"
	fi
	upstream="$1"

	# Upstream  after setup: main(B)  foo(A)  bar(A)  baz(A)
	# Workbench after setup: main(A)                  baz(A)  next(A)
	test_expect_success "setup upstream repository and workbench" '
		setup_upstream "$upstream" &&
		rm -rf workbench &&
		git clone "$upstream" workbench &&
		(
			cd workbench &&
			git update-ref refs/heads/main $A &&
			git update-ref refs/heads/baz $A &&
			git update-ref refs/heads/next $A &&
			# Try to make a stable fixed width for abbreviated commit ID,
			# this fixed-width oid will be replaced with "<OID>".
			git config core.abbrev 7 &&
			git config advice.pushUpdateRejected false
		) &&
		# The upstream repository provides services using the HTTP protocol.
		if ! test "$upstream" = "upstream.git"
		then
			git -C workbench remote set-url origin "$HTTPD_URL/smart/upstream.git"
		fi
	'
}

run_git_push_porcelain_output_test() {
	case $1 in
	http)
		PROTOCOL="HTTP protocol"
		URL_PREFIX="http://.*"
		;;
	file)
		PROTOCOL="builtin protocol"
		URL_PREFIX=".*"
		;;
	esac

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# git-push         : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. git-push --porcelain ($PROTOCOL)" '
		test_when_finished "setup_upstream \"$upstream\"" &&
		test_must_fail git -C workbench push --porcelain origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-\EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		>  	<COMMIT-B>:refs/heads/bar	<COMMIT-A>..<COMMIT-B>
		> -	:refs/heads/foo	[deleted]
		> *	refs/heads/next:refs/heads/next	[new branch]
		> !	refs/heads/main:refs/heads/main	[rejected] (non-fast-forward)
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-B> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-B> refs/heads/main
		<COMMIT-A> refs/heads/next
		EOF
		test_cmp expect actual
	'

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# git-push         : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. git-push --porcelain --force ($PROTOCOL)" '
		test_when_finished "setup_upstream \"$upstream\"" &&
		git -C workbench push --porcelain --force origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		>  	<COMMIT-B>:refs/heads/bar	<COMMIT-A>..<COMMIT-B>
		> -	:refs/heads/foo	[deleted]
		> +	refs/heads/main:refs/heads/main	<COMMIT-B>...<COMMIT-A> (forced update)
		> *	refs/heads/next:refs/heads/next	[new branch]
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-B> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/main
		<COMMIT-A> refs/heads/next
		EOF
		test_cmp expect actual
	'

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# git-push         : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. git push --porcelain --atomic ($PROTOCOL)" '
		test_when_finished "setup_upstream \"$upstream\"" &&
		test_must_fail git -C workbench push --porcelain --atomic origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		> !	<COMMIT-B>:refs/heads/bar	[rejected] (atomic push failed)
		> !	(delete):refs/heads/foo	[rejected] (atomic push failed)
		> !	refs/heads/main:refs/heads/main	[rejected] (non-fast-forward)
		> !	refs/heads/next:refs/heads/next	[rejected] (atomic push failed)
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-A> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/foo
		<COMMIT-B> refs/heads/main
		EOF
		test_cmp expect actual
	'

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# git-push         : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. pre-receive hook declined ($PROTOCOL)" '
		test_when_finished "rm -f \"$upstream/hooks/pre-receive\" &&
			setup_upstream \"$upstream\"" &&
		test_hook --setup -C "$upstream" pre-receive <<-EOF &&
			exit 1
		EOF
		test_must_fail git -C workbench push --porcelain --force origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		> !	<COMMIT-B>:refs/heads/bar	[remote rejected] (pre-receive hook declined)
		> !	:refs/heads/foo	[remote rejected] (pre-receive hook declined)
		> !	refs/heads/main:refs/heads/main	[remote rejected] (pre-receive hook declined)
		> !	refs/heads/next:refs/heads/next	[remote rejected] (pre-receive hook declined)
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-A> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/foo
		<COMMIT-B> refs/heads/main
		EOF
		test_cmp expect actual
	'

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# git-push         : main(A)                          next(A)
	test_expect_success ".. non-fastforward push ($PROTOCOL)" '
		test_when_finished "setup_upstream \"$upstream\"" &&
		(
			cd workbench &&
			test_must_fail git push --porcelain origin \
				main \
				next
		) >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-EOF &&
		> To <URL/of/upstream.git>
		> *	refs/heads/next:refs/heads/next	[new branch]
		> !	refs/heads/main:refs/heads/main	[rejected] (non-fast-forward)
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-A> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/foo
		<COMMIT-B> refs/heads/main
		<COMMIT-A> refs/heads/next
		EOF
		test_cmp expect actual
	'

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# git-push         : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. git push --porcelain --atomic --force ($PROTOCOL)" '
		git -C workbench push --porcelain --atomic --force origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-\EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		>  	<COMMIT-B>:refs/heads/bar	<COMMIT-A>..<COMMIT-B>
		> -	:refs/heads/foo	[deleted]
		> +	refs/heads/main:refs/heads/main	<COMMIT-B>...<COMMIT-A> (forced update)
		> *	refs/heads/next:refs/heads/next	[new branch]
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-B> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/main
		<COMMIT-A> refs/heads/next
		EOF
		test_cmp expect actual
	'
}

run_git_push_dry_run_porcelain_output_test() {
	case $1 in
	http)
		PROTOCOL="HTTP protocol"
		URL_PREFIX="http://.*"
		;;
	file)
		PROTOCOL="builtin protocol"
		URL_PREFIX=".*"
		;;
	esac

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# git-push         : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. git-push --porcelain --dry-run ($PROTOCOL)" '
		test_must_fail git -C workbench push --porcelain --dry-run origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		>  	<COMMIT-B>:refs/heads/bar	<COMMIT-A>..<COMMIT-B>
		> -	:refs/heads/foo	[deleted]
		> *	refs/heads/next:refs/heads/next	[new branch]
		> !	refs/heads/main:refs/heads/main	[rejected] (non-fast-forward)
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-A> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/foo
		<COMMIT-B> refs/heads/main
		EOF
		test_cmp expect actual
	'

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# push             : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. git-push --porcelain --dry-run --force ($PROTOCOL)" '
		git -C workbench push --porcelain --dry-run --force origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		>  	<COMMIT-B>:refs/heads/bar	<COMMIT-A>..<COMMIT-B>
		> -	:refs/heads/foo	[deleted]
		> +	refs/heads/main:refs/heads/main	<COMMIT-B>...<COMMIT-A> (forced update)
		> *	refs/heads/next:refs/heads/next	[new branch]
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-A> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/foo
		<COMMIT-B> refs/heads/main
		EOF
		test_cmp expect actual
	'

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# git-push         : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. git-push --porcelain --dry-run --atomic ($PROTOCOL)" '
		test_must_fail git -C workbench push --porcelain --dry-run --atomic origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		> !	<COMMIT-B>:refs/heads/bar	[rejected] (atomic push failed)
		> !	(delete):refs/heads/foo	[rejected] (atomic push failed)
		> !	refs/heads/main:refs/heads/main	[rejected] (non-fast-forward)
		> !	refs/heads/next:refs/heads/next	[rejected] (atomic push failed)
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-A> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/foo
		<COMMIT-B> refs/heads/main
		EOF
		test_cmp expect actual
	'

	# Refs of upstream : main(B)  foo(A)  bar(A)  baz(A)
	# Refs of workbench: main(A)                  baz(A)  next(A)
	# push             : main(A)  NULL    (B)     baz(A)  next(A)
	test_expect_success ".. git-push --porcelain --dry-run --atomic --force ($PROTOCOL)" '
		git -C workbench push --porcelain --dry-run --atomic --force origin \
			main \
			:refs/heads/foo \
			$B:bar \
			baz \
			next >out &&
		make_user_friendly_and_stable_output <out >actual &&
		format_and_save_expect <<-EOF &&
		> To <URL/of/upstream.git>
		> =	refs/heads/baz:refs/heads/baz	[up to date]
		>  	<COMMIT-B>:refs/heads/bar	<COMMIT-A>..<COMMIT-B>
		> -	:refs/heads/foo	[deleted]
		> +	refs/heads/main:refs/heads/main	<COMMIT-B>...<COMMIT-A> (forced update)
		> *	refs/heads/next:refs/heads/next	[new branch]
		> Done
		EOF
		test_cmp expect actual &&

		git -C "$upstream" show-ref >out &&
		make_user_friendly_and_stable_output <out >actual &&
		cat >expect <<-EOF &&
		<COMMIT-A> refs/heads/bar
		<COMMIT-A> refs/heads/baz
		<COMMIT-A> refs/heads/foo
		<COMMIT-B> refs/heads/main
		EOF
		test_cmp expect actual
	'
}

setup_upstream_and_workbench upstream.git

run_git_push_porcelain_output_test file

setup_upstream_and_workbench upstream.git

run_git_push_dry_run_porcelain_output_test file

ROOT_PATH="$PWD"
. "$TEST_DIRECTORY"/lib-gpg.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
. "$TEST_DIRECTORY"/lib-terminal.sh
start_httpd
setup_askpass_helper

setup_upstream_and_workbench "$HTTPD_DOCUMENT_ROOT_PATH/upstream.git"

run_git_push_porcelain_output_test http

setup_upstream_and_workbench "$HTTPD_DOCUMENT_ROOT_PATH/upstream.git"

run_git_push_dry_run_porcelain_output_test http

test_done
