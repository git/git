#!/bin/sh

test_description='tests for git clone -c key=value'
. ./test-lib.sh

test_expect_success 'clone -c sets config in cloned repo' '
	rm -rf child &&
	git clone -c core.foo=bar . child &&
	echo bar >expect &&
	git --git-dir=child/.git config core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'clone -c can set multi-keys' '
	rm -rf child &&
	git clone -c core.foo=bar -c core.foo=baz . child &&
	{ echo bar; echo baz; } >expect &&
	git --git-dir=child/.git config --get-all core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'clone -c can set multi-keys, including some empty' '
	rm -rf child &&
	git clone -c credential.helper= -c credential.helper=hi . child &&
	printf "%s\n" "" hi >expect &&
	git --git-dir=child/.git config --get-all credential.helper >actual &&
	test_cmp expect actual
'

test_expect_success 'clone -c without a value is boolean true' '
	rm -rf child &&
	git clone -c core.foo . child &&
	echo true >expect &&
	git --git-dir=child/.git config --bool core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'clone -c config is available during clone' '
	echo content >file &&
	git add file &&
	git commit -m one &&
	rm -rf child &&
	git clone -c core.autocrlf . child &&
	printf "content\\r\\n" >expect &&
	test_cmp expect child/file
'

test_expect_success 'clone -c remote.origin.fetch=<refspec> works' '
	rm -rf child &&
	git update-ref refs/grab/it refs/heads/master &&
	git update-ref refs/leave/out refs/heads/master &&
	git clone -c "remote.origin.fetch=+refs/grab/*:refs/grab/*" . child &&
	git -C child for-each-ref --format="%(refname)" >actual &&

	cat >expect <<-\EOF &&
	refs/grab/it
	refs/heads/master
	refs/remotes/origin/HEAD
	refs/remotes/origin/master
	EOF
	test_cmp expect actual
'

test_expect_success 'git -c remote.origin.fetch=<refspec> clone works' '
	rm -rf child &&
	git -c "remote.origin.fetch=+refs/grab/*:refs/grab/*" clone . child &&
	git -C child for-each-ref --format="%(refname)" >actual &&

	cat >expect <<-\EOF &&
	refs/grab/it
	refs/heads/master
	refs/remotes/origin/HEAD
	refs/remotes/origin/master
	EOF
	test_cmp expect actual
'

test_expect_success 'clone -c remote.<remote>.fetch=<refspec> --origin=<name>' '
	rm -rf child &&
	git clone --origin=upstream \
		  -c "remote.upstream.fetch=+refs/grab/*:refs/grab/*" \
		  -c "remote.origin.fetch=+refs/leave/*:refs/leave/*" \
		  . child &&
	git -C child for-each-ref --format="%(refname)" >actual &&

	cat >expect <<-\EOF &&
	refs/grab/it
	refs/heads/master
	refs/remotes/upstream/HEAD
	refs/remotes/upstream/master
	EOF
	test_cmp expect actual
'

# Tests for the hidden file attribute on windows
is_hidden () {
	# Use the output of `attrib`, ignore the absolute path
	case "$("$SYSTEMROOT"/system32/attrib "$1")" in *H*?:*) return 0;; esac
	return 1
}

test_expect_success MINGW 'clone -c core.hideDotFiles' '
	test_commit attributes .gitattributes "" &&
	rm -rf child &&
	git clone -c core.hideDotFiles=false . child &&
	! is_hidden child/.gitattributes &&
	rm -rf child &&
	git clone -c core.hideDotFiles=dotGitOnly . child &&
	! is_hidden child/.gitattributes &&
	rm -rf child &&
	git clone -c core.hideDotFiles=true . child &&
	is_hidden child/.gitattributes
'

test_done
