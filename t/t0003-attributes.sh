#!/bin/sh

test_description=gitattributes

TEST_PASSES_SANITIZE_LEAK=true
TEST_CREATE_REPO_NO_TEMPLATE=1
. ./test-lib.sh

attr_check_basic () {
	path="$1" expect="$2" git_opts="$3" &&

	git $git_opts check-attr test -- "$path" >actual 2>err &&
	echo "$path: test: $expect" >expect &&
	test_cmp expect actual
}

attr_check () {
	attr_check_basic "$@" &&
	test_must_be_empty err
}

attr_check_quote () {
	path="$1" quoted_path="$2" expect="$3" &&

	git check-attr test -- "$path" >actual &&
	echo "\"$quoted_path\": test: $expect" >expect &&
	test_cmp expect actual
}

attr_check_source () {
	path="$1" expect="$2" source="$3" git_opts="$4" &&

	echo "$path: test: $expect" >expect &&

	git $git_opts check-attr --source $source test -- "$path" >actual 2>err &&
	test_cmp expect actual &&
	test_must_be_empty err &&

	git $git_opts --attr-source="$source" check-attr test -- "$path" >actual 2>err &&
	test_cmp expect actual &&
	test_must_be_empty err

	GIT_ATTR_SOURCE="$source" git $git_opts check-attr test -- "$path" >actual 2>err &&
	test_cmp expect actual &&
	test_must_be_empty err
}

test_expect_success 'open-quoted pathname' '
	echo "\"a test=a" >.gitattributes &&
	attr_check a unspecified
'

test_expect_success 'setup' '
	mkdir -p a/b/d a/c b &&
	(
		echo "[attr]notest !test" &&
		echo "\" d \"	test=d" &&
		echo " e	test=e" &&
		echo " e\"	test=e" &&
		echo "f	test=f" &&
		echo "a/i test=a/i" &&
		echo "onoff test -test" &&
		echo "offon -test test" &&
		echo "no notest" &&
		echo "A/e/F test=A/e/F"
	) >.gitattributes &&
	(
		echo "g test=a/g" &&
		echo "b/g test=a/b/g"
	) >a/.gitattributes &&
	(
		echo "h test=a/b/h" &&
		echo "d/* test=a/b/d/*" &&
		echo "d/yes notest"
	) >a/b/.gitattributes &&
	(
		echo "global test=global"
	) >"$HOME"/global-gitattributes &&
	cat <<-EOF >expect-all
	f: test: f
	a/f: test: f
	a/c/f: test: f
	a/g: test: a/g
	a/b/g: test: a/b/g
	b/g: test: unspecified
	a/b/h: test: a/b/h
	a/b/d/g: test: a/b/d/*
	onoff: test: unset
	offon: test: set
	no: notest: set
	no: test: unspecified
	a/b/d/no: notest: set
	a/b/d/no: test: a/b/d/*
	a/b/d/yes: notest: set
	a/b/d/yes: test: unspecified
	EOF
'

test_expect_success 'setup branches' '
	mkdir -p foo/bar &&
	test_commit --printf "add .gitattributes" foo/bar/.gitattributes \
		"f test=f\na/i test=n\n" tag-1 &&
	test_commit --printf "add .gitattributes" foo/bar/.gitattributes \
		"g test=g\na/i test=m\n" tag-2 &&
	rm foo/bar/.gitattributes
'

test_expect_success 'command line checks' '
	test_must_fail git check-attr &&
	test_must_fail git check-attr -- &&
	test_must_fail git check-attr test &&
	test_must_fail git check-attr test -- &&
	test_must_fail git check-attr -- f &&
	test_must_fail git check-attr --source &&
	test_must_fail git check-attr --source not-a-valid-ref &&
	echo "f" | test_must_fail git check-attr --stdin &&
	echo "f" | test_must_fail git check-attr --stdin -- f &&
	echo "f" | test_must_fail git check-attr --stdin test -- f &&
	test_must_fail git check-attr "" -- f
'

test_expect_success 'attribute test' '

	attr_check " d " d &&
	attr_check e e &&
	attr_check_quote e\" e\\\" e &&

	attr_check f f &&
	attr_check a/f f &&
	attr_check a/c/f f &&
	attr_check a/g a/g &&
	attr_check a/b/g a/b/g &&
	attr_check b/g unspecified &&
	attr_check a/b/h a/b/h &&
	attr_check a/b/d/g "a/b/d/*" &&
	attr_check onoff unset &&
	attr_check offon set &&
	attr_check no unspecified &&
	attr_check a/b/d/no "a/b/d/*" &&
	attr_check a/b/d/yes unspecified
'

test_expect_success 'attribute matching is case sensitive when core.ignorecase=0' '

	attr_check F unspecified "-c core.ignorecase=0" &&
	attr_check a/F unspecified "-c core.ignorecase=0" &&
	attr_check a/c/F unspecified "-c core.ignorecase=0" &&
	attr_check a/G unspecified "-c core.ignorecase=0" &&
	attr_check a/B/g a/g "-c core.ignorecase=0" &&
	attr_check a/b/G unspecified "-c core.ignorecase=0" &&
	attr_check a/b/H unspecified "-c core.ignorecase=0" &&
	attr_check a/b/D/g a/g "-c core.ignorecase=0" &&
	attr_check oNoFf unspecified "-c core.ignorecase=0" &&
	attr_check oFfOn unspecified "-c core.ignorecase=0" &&
	attr_check NO unspecified "-c core.ignorecase=0" &&
	attr_check a/b/D/NO unspecified "-c core.ignorecase=0" &&
	attr_check a/b/d/YES a/b/d/* "-c core.ignorecase=0" &&
	attr_check a/E/f f "-c core.ignorecase=0"

'

test_expect_success 'attribute matching is case insensitive when core.ignorecase=1' '

	attr_check F f "-c core.ignorecase=1" &&
	attr_check a/F f "-c core.ignorecase=1" &&
	attr_check a/c/F f "-c core.ignorecase=1" &&
	attr_check a/G a/g "-c core.ignorecase=1" &&
	attr_check a/B/g a/b/g "-c core.ignorecase=1" &&
	attr_check a/b/G a/b/g "-c core.ignorecase=1" &&
	attr_check a/b/H a/b/h "-c core.ignorecase=1" &&
	attr_check a/b/D/g "a/b/d/*" "-c core.ignorecase=1" &&
	attr_check oNoFf unset "-c core.ignorecase=1" &&
	attr_check oFfOn set "-c core.ignorecase=1" &&
	attr_check NO unspecified "-c core.ignorecase=1" &&
	attr_check a/b/D/NO "a/b/d/*" "-c core.ignorecase=1" &&
	attr_check a/b/d/YES unspecified "-c core.ignorecase=1" &&
	attr_check a/E/f "A/e/F" "-c core.ignorecase=1"

'

test_expect_success CASE_INSENSITIVE_FS 'additional case insensitivity tests' '
	attr_check a/B/D/g a/g "-c core.ignorecase=0" &&
	attr_check A/B/D/NO unspecified "-c core.ignorecase=0" &&
	attr_check A/b/h a/b/h "-c core.ignorecase=1" &&
	attr_check a/B/D/g "a/b/d/*" "-c core.ignorecase=1" &&
	attr_check A/B/D/NO "a/b/d/*" "-c core.ignorecase=1"
'

test_expect_success 'unnormalized paths' '
	attr_check ./f f &&
	attr_check ./a/g a/g &&
	attr_check a/./g a/g &&
	attr_check a/c/../b/g a/b/g
'

test_expect_success 'relative paths' '
	(cd a && attr_check ../f f) &&
	(cd a && attr_check f f) &&
	(cd a && attr_check i a/i) &&
	(cd a && attr_check g a/g) &&
	(cd a && attr_check b/g a/b/g) &&
	(cd b && attr_check ../a/f f) &&
	(cd b && attr_check ../a/g a/g) &&
	(cd b && attr_check ../a/b/g a/b/g)
'

test_expect_success 'prefixes are not confused with leading directories' '
	attr_check a_plus/g unspecified &&
	cat >expect <<-\EOF &&
	a/g: test: a/g
	a_plus/g: test: unspecified
	EOF
	git check-attr test a/g a_plus/g >actual &&
	test_cmp expect actual
'

test_expect_success 'core.attributesfile' '
	attr_check global unspecified &&
	git config core.attributesfile "$HOME/global-gitattributes" &&
	attr_check global global &&
	git config core.attributesfile "~/global-gitattributes" &&
	attr_check global global &&
	echo "global test=precedence" >>.gitattributes &&
	attr_check global precedence
'

test_expect_success 'attribute test: read paths from stdin' '
	grep -v notest <expect-all >expect &&
	sed -e "s/:.*//" <expect | git check-attr --stdin test >actual &&
	test_cmp expect actual
'

test_expect_success 'setup --all option' '
	grep -v unspecified <expect-all | sort >specified-all &&
	sed -e "s/:.*//" <expect-all | uniq >stdin-all
'

test_expect_success 'attribute test: --all option' '
	git check-attr --stdin --all <stdin-all >tmp &&
	sort tmp >actual &&
	test_cmp specified-all actual
'

test_expect_success 'attribute test: --cached option' '
	git check-attr --cached --stdin --all <stdin-all >tmp &&
	sort tmp >actual &&
	test_must_be_empty actual &&
	git add .gitattributes a/.gitattributes a/b/.gitattributes &&
	git check-attr --cached --stdin --all <stdin-all >tmp &&
	sort tmp >actual &&
	test_cmp specified-all actual
'

test_expect_success 'root subdir attribute test' '
	attr_check a/i a/i &&
	attr_check subdir/a/i unspecified
'

test_expect_success 'negative patterns' '
	echo "!f test=bar" >.gitattributes &&
	git check-attr test -- '"'"'!f'"'"' 2>errors &&
	test_i18ngrep "Negative patterns are ignored" errors
'

test_expect_success 'patterns starting with exclamation' '
	echo "\!f test=foo" >.gitattributes &&
	attr_check "!f" foo
'

test_expect_success '"**" test' '
	echo "**/f foo=bar" >.gitattributes &&
	cat <<\EOF >expect &&
f: foo: bar
a/f: foo: bar
a/b/f: foo: bar
a/b/c/f: foo: bar
EOF
	git check-attr foo -- "f" >actual 2>err &&
	git check-attr foo -- "a/f" >>actual 2>>err &&
	git check-attr foo -- "a/b/f" >>actual 2>>err &&
	git check-attr foo -- "a/b/c/f" >>actual 2>>err &&
	test_cmp expect actual &&
	test_must_be_empty err
'

test_expect_success '"**" with no slashes test' '
	echo "a**f foo=bar" >.gitattributes &&
	git check-attr foo -- "f" >actual &&
	cat <<\EOF >expect &&
f: foo: unspecified
af: foo: bar
axf: foo: bar
a/f: foo: unspecified
a/b/f: foo: unspecified
a/b/c/f: foo: unspecified
EOF
	git check-attr foo -- "f" >actual 2>err &&
	git check-attr foo -- "af" >>actual 2>err &&
	git check-attr foo -- "axf" >>actual 2>err &&
	git check-attr foo -- "a/f" >>actual 2>>err &&
	git check-attr foo -- "a/b/f" >>actual 2>>err &&
	git check-attr foo -- "a/b/c/f" >>actual 2>>err &&
	test_cmp expect actual &&
	test_must_be_empty err
'

test_expect_success 'using --git-dir and --work-tree' '
	mkdir unreal real &&
	git init real &&
	echo "file test=in-real" >real/.gitattributes &&
	(
		cd unreal &&
		attr_check file in-real "--git-dir ../real/.git --work-tree ../real"
	)
'

test_expect_success 'using --source' '
	attr_check_source foo/bar/f f tag-1 &&
	attr_check_source foo/bar/a/i n tag-1 &&
	attr_check_source foo/bar/f unspecified tag-2 &&
	attr_check_source foo/bar/a/i m tag-2 &&
	attr_check_source foo/bar/g g tag-2 &&
	attr_check_source foo/bar/g unspecified tag-1
'

test_expect_success 'setup bare' '
	git clone --template= --bare . bare.git
'

test_expect_success 'bare repository: check that .gitattribute is ignored' '
	(
		cd bare.git &&
		(
			echo "f	test=f" &&
			echo "a/i test=a/i"
		) >.gitattributes &&
		attr_check f unspecified &&
		attr_check a/f unspecified &&
		attr_check a/c/f unspecified &&
		attr_check a/i unspecified &&
		attr_check subdir/a/i unspecified
	)
'

test_expect_success 'bare repository: with --source' '
	(
		cd bare.git &&
		attr_check_source foo/bar/f f tag-1 &&
		attr_check_source foo/bar/a/i n tag-1 &&
		attr_check_source foo/bar/f unspecified tag-2 &&
		attr_check_source foo/bar/a/i m tag-2 &&
		attr_check_source foo/bar/g g tag-2 &&
		attr_check_source foo/bar/g unspecified tag-1
	)
'

test_expect_success 'bare repository: check that --cached honors index' '
	(
		cd bare.git &&
		GIT_INDEX_FILE=../.git/index \
		git check-attr --cached --stdin --all <../stdin-all |
		sort >actual &&
		test_cmp ../specified-all actual
	)
'

test_expect_success 'bare repository: test info/attributes' '
	(
		cd bare.git &&
		mkdir info &&
		(
			echo "f	test=f" &&
			echo "a/i test=a/i"
		) >info/attributes &&
		attr_check f f &&
		attr_check a/f f &&
		attr_check a/c/f f &&
		attr_check a/i a/i &&
		attr_check subdir/a/i unspecified
	)
'

test_expect_success 'binary macro expanded by -a' '
	echo "file binary" >.gitattributes &&
	cat >expect <<-\EOF &&
	file: binary: set
	file: diff: unset
	file: merge: unset
	file: text: unset
	EOF
	git check-attr -a file >actual &&
	test_cmp expect actual
'

test_expect_success 'query binary macro directly' '
	echo "file binary" >.gitattributes &&
	echo file: binary: set >expect &&
	git check-attr binary file >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'set up symlink tests' '
	echo "* test" >attr &&
	rm -f .gitattributes
'

test_expect_success SYMLINKS 'symlinks respected in core.attributesFile' '
	test_when_finished "rm symlink" &&
	ln -s attr symlink &&
	test_config core.attributesFile "$(pwd)/symlink" &&
	attr_check file set
'

test_expect_success SYMLINKS 'symlinks respected in info/attributes' '
	test_when_finished "rm .git/info/attributes" &&
	mkdir .git/info &&
	ln -s ../../attr .git/info/attributes &&
	attr_check file set
'

test_expect_success SYMLINKS 'symlinks not respected in-tree' '
	test_when_finished "rm -rf .gitattributes subdir" &&
	ln -s attr .gitattributes &&
	mkdir subdir &&
	ln -s ../attr subdir/.gitattributes &&
	attr_check_basic subdir/file unspecified &&
	test_i18ngrep "unable to access.*gitattributes" err
'

test_expect_success 'large attributes line ignored in tree' '
	test_when_finished "rm .gitattributes" &&
	printf "path %02043d" 1 >.gitattributes &&
	git check-attr --all path >actual 2>err &&
	echo "warning: ignoring overly long attributes line 1" >expect &&
	test_cmp expect err &&
	test_must_be_empty actual
'

test_expect_success 'large attributes line ignores trailing content in tree' '
	test_when_finished "rm .gitattributes" &&
	# older versions of Git broke lines at 2048 bytes; the 2045 bytes
	# of 0-padding here is accounting for the three bytes of "a 1", which
	# would knock "trailing" to the "next" line, where it would be
	# erroneously parsed.
	printf "a %02045dtrailing attribute\n" 1 >.gitattributes &&
	git check-attr --all trailing >actual 2>err &&
	echo "warning: ignoring overly long attributes line 1" >expect &&
	test_cmp expect err &&
	test_must_be_empty actual
'

test_expect_success EXPENSIVE 'large attributes file ignored in tree' '
	test_when_finished "rm .gitattributes" &&
	dd if=/dev/zero of=.gitattributes bs=1048576 count=101 2>/dev/null &&
	git check-attr --all path >/dev/null 2>err &&
	echo "warning: ignoring overly large gitattributes file ${SQ}.gitattributes${SQ}" >expect &&
	test_cmp expect err
'

test_expect_success 'large attributes line ignored in index' '
	test_when_finished "git update-index --remove .gitattributes" &&
	blob=$(printf "path %02043d" 1 | git hash-object -w --stdin) &&
	git update-index --add --cacheinfo 100644,$blob,.gitattributes &&
	git check-attr --cached --all path >actual 2>err &&
	echo "warning: ignoring overly long attributes line 1" >expect &&
	test_cmp expect err &&
	test_must_be_empty actual
'

test_expect_success 'large attributes line ignores trailing content in index' '
	test_when_finished "git update-index --remove .gitattributes" &&
	blob=$(printf "a %02045dtrailing attribute\n" 1 | git hash-object -w --stdin) &&
	git update-index --add --cacheinfo 100644,$blob,.gitattributes &&
	git check-attr --cached --all trailing >actual 2>err &&
	echo "warning: ignoring overly long attributes line 1" >expect &&
	test_cmp expect err &&
	test_must_be_empty actual
'

test_expect_success EXPENSIVE 'large attributes file ignored in index' '
	test_when_finished "git update-index --remove .gitattributes" &&
	blob=$(dd if=/dev/zero bs=1048576 count=101 2>/dev/null | git hash-object -w --stdin) &&
	git update-index --add --cacheinfo 100644,$blob,.gitattributes &&
	git check-attr --cached --all path >/dev/null 2>err &&
	echo "warning: ignoring overly large gitattributes blob ${SQ}.gitattributes${SQ}" >expect &&
	test_cmp expect err
'

test_done
