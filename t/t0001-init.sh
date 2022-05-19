#!/bin/sh

test_description='but init'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

check_config () {
	if test_path_is_dir "$1" &&
	   test_path_is_file "$1/config" && test_path_is_dir "$1/refs"
	then
		: happy
	else
		echo "expected a directory $1, a file $1/config and $1/refs"
		return 1
	fi

	if test_have_prereq POSIXPERM && test -x "$1/config"
	then
		echo "$1/config is executable?"
		return 1
	fi

	bare=$(cd "$1" && but config --bool core.bare)
	worktree=$(cd "$1" && but config core.worktree) ||
	worktree=unset

	test "$bare" = "$2" && test "$worktree" = "$3" || {
		echo "expected bare=$2 worktree=$3"
		echo "     got bare=$bare worktree=$worktree"
		return 1
	}
}

test_expect_success 'plain' '
	but init plain &&
	check_config plain/.but false unset
'

test_expect_success 'plain nested in bare' '
	(
		but init --bare bare-ancestor.but &&
		cd bare-ancestor.but &&
		mkdir plain-nested &&
		cd plain-nested &&
		but init
	) &&
	check_config bare-ancestor.but/plain-nested/.but false unset
'

test_expect_success 'plain through aliased command, outside any but repo' '
	(
		HOME=$(pwd)/alias-config &&
		export HOME &&
		mkdir alias-config &&
		echo "[alias] aliasedinit = init" >alias-config/.butconfig &&

		GIT_CEILING_DIRECTORIES=$(pwd) &&
		export GIT_CEILING_DIRECTORIES &&

		mkdir plain-aliased &&
		cd plain-aliased &&
		but aliasedinit
	) &&
	check_config plain-aliased/.but false unset
'

test_expect_success 'plain nested through aliased command' '
	(
		but init plain-ancestor-aliased &&
		cd plain-ancestor-aliased &&
		echo "[alias] aliasedinit = init" >>.but/config &&
		mkdir plain-nested &&
		cd plain-nested &&
		but aliasedinit
	) &&
	check_config plain-ancestor-aliased/plain-nested/.but false unset
'

test_expect_success 'plain nested in bare through aliased command' '
	(
		but init --bare bare-ancestor-aliased.but &&
		cd bare-ancestor-aliased.but &&
		echo "[alias] aliasedinit = init" >>config &&
		mkdir plain-nested &&
		cd plain-nested &&
		but aliasedinit
	) &&
	check_config bare-ancestor-aliased.but/plain-nested/.but false unset
'

test_expect_success 'No extra GIT_* on alias scripts' '
	write_script script <<-\EOF &&
	env |
		sed -n \
			-e "/^GIT_PREFIX=/d" \
			-e "/^GIT_TEXTDOMAINDIR=/d" \
			-e "/^GIT_TRACE2_PARENT/d" \
			-e "/^GIT_/s/=.*//p" |
		sort
	EOF
	./script >expected &&
	but config alias.script \!./script &&
	( mkdir sub && cd sub && but script >../actual ) &&
	test_cmp expected actual
'

test_expect_success 'plain with GIT_WORK_TREE' '
	mkdir plain-wt &&
	test_must_fail env GIT_WORK_TREE="$(pwd)/plain-wt" but init plain-wt
'

test_expect_success 'plain bare' '
	but --bare init plain-bare-1 &&
	check_config plain-bare-1 true unset
'

test_expect_success 'plain bare with GIT_WORK_TREE' '
	mkdir plain-bare-2 &&
	test_must_fail \
		env GIT_WORK_TREE="$(pwd)/plain-bare-2" \
		but --bare init plain-bare-2
'

test_expect_success 'GIT_DIR bare' '
	mkdir but-dir-bare.but &&
	GIT_DIR=but-dir-bare.but but init &&
	check_config but-dir-bare.but true unset
'

test_expect_success 'init --bare' '
	but init --bare init-bare.but &&
	check_config init-bare.but true unset
'

test_expect_success 'GIT_DIR non-bare' '

	(
		mkdir non-bare &&
		cd non-bare &&
		GIT_DIR=.but but init
	) &&
	check_config non-bare/.but false unset
'

test_expect_success 'GIT_DIR & GIT_WORK_TREE (1)' '

	(
		mkdir but-dir-wt-1.but &&
		GIT_WORK_TREE=$(pwd) GIT_DIR=but-dir-wt-1.but but init
	) &&
	check_config but-dir-wt-1.but false "$(pwd)"
'

test_expect_success 'GIT_DIR & GIT_WORK_TREE (2)' '
	mkdir but-dir-wt-2.but &&
	test_must_fail env \
		GIT_WORK_TREE="$(pwd)" \
		GIT_DIR=but-dir-wt-2.but \
		but --bare init
'

test_expect_success 'reinit' '

	(
		mkdir again &&
		cd again &&
		but -c init.defaultBranch=initial init >out1 2>err1 &&
		but init >out2 2>err2
	) &&
	test_i18ngrep "Initialized empty" again/out1 &&
	test_i18ngrep "Reinitialized existing" again/out2 &&
	test_must_be_empty again/err1 &&
	test_must_be_empty again/err2
'

test_expect_success 'init with --template' '
	mkdir template-source &&
	echo content >template-source/file &&
	but init --template=template-source template-custom &&
	test_cmp template-source/file template-custom/.but/file
'

test_expect_success 'init with --template (blank)' '
	but init template-plain &&
	test_path_is_file template-plain/.but/info/exclude &&
	but init --template= template-blank &&
	test_path_is_missing template-blank/.but/info/exclude
'

init_no_templatedir_env () {
	(
		sane_unset GIT_TEMPLATE_DIR &&
		NO_SET_GIT_TEMPLATE_DIR=t &&
		export NO_SET_GIT_TEMPLATE_DIR &&
		but init "$1"
	)
}

test_expect_success 'init with init.templatedir set' '
	mkdir templatedir-source &&
	echo Content >templatedir-source/file &&
	test_config_global init.templatedir "${HOME}/templatedir-source" &&

	init_no_templatedir_env templatedir-set &&
	test_cmp templatedir-source/file templatedir-set/.but/file
'

test_expect_success 'init with init.templatedir using ~ expansion' '
	mkdir -p templatedir-source &&
	echo Content >templatedir-source/file &&
	test_config_global init.templatedir "~/templatedir-source" &&

	init_no_templatedir_env templatedir-expansion &&
	test_cmp templatedir-source/file templatedir-expansion/.but/file
'

test_expect_success 'init --bare/--shared overrides system/global config' '
	test_config_global core.bare false &&
	test_config_global core.sharedRepository 0640 &&
	but init --bare --shared=0666 init-bare-shared-override &&
	check_config init-bare-shared-override true unset &&
	test x0666 = \
	x$(but config -f init-bare-shared-override/config core.sharedRepository)
'

test_expect_success 'init honors global core.sharedRepository' '
	test_config_global core.sharedRepository 0666 &&
	but init shared-honor-global &&
	test x0666 = \
	x$(but config -f shared-honor-global/.but/config core.sharedRepository)
'

test_expect_success 'init allows insanely long --template' '
	but init --template=$(printf "x%09999dx" 1) test
'

test_expect_success 'init creates a new directory' '
	rm -fr newdir &&
	but init newdir &&
	test_path_is_dir newdir/.but/refs
'

test_expect_success 'init creates a new bare directory' '
	rm -fr newdir &&
	but init --bare newdir &&
	test_path_is_dir newdir/refs
'

test_expect_success 'init recreates a directory' '
	rm -fr newdir &&
	mkdir newdir &&
	but init newdir &&
	test_path_is_dir newdir/.but/refs
'

test_expect_success 'init recreates a new bare directory' '
	rm -fr newdir &&
	mkdir newdir &&
	but init --bare newdir &&
	test_path_is_dir newdir/refs
'

test_expect_success 'init creates a new deep directory' '
	rm -fr newdir &&
	but init newdir/a/b/c &&
	test_path_is_dir newdir/a/b/c/.but/refs
'

test_expect_success POSIXPERM 'init creates a new deep directory (umask vs. shared)' '
	rm -fr newdir &&
	(
		# Leading directories should honor umask while
		# the repository itself should follow "shared"
		mkdir newdir &&
		# Remove a default ACL if possible.
		(setfacl -k newdir 2>/dev/null || true) &&
		umask 002 &&
		but init --bare --shared=0660 newdir/a/b/c &&
		test_path_is_dir newdir/a/b/c/refs &&
		ls -ld newdir/a newdir/a/b > lsab.out &&
		! grep -v "^drwxrw[sx]r-x" lsab.out &&
		ls -ld newdir/a/b/c > lsc.out &&
		! grep -v "^drwxrw[sx]---" lsc.out
	)
'

test_expect_success 'init notices EEXIST (1)' '
	rm -fr newdir &&
	>newdir &&
	test_must_fail but init newdir &&
	test_path_is_file newdir
'

test_expect_success 'init notices EEXIST (2)' '
	rm -fr newdir &&
	mkdir newdir &&
	>newdir/a &&
	test_must_fail but init newdir/a/b &&
	test_path_is_file newdir/a
'

test_expect_success POSIXPERM,SANITY 'init notices EPERM' '
	test_when_finished "chmod +w newdir" &&
	rm -fr newdir &&
	mkdir newdir &&
	chmod -w newdir &&
	test_must_fail but init newdir/a/b
'

test_expect_success 'init creates a new bare directory with global --bare' '
	rm -rf newdir &&
	but --bare init newdir &&
	test_path_is_dir newdir/refs
'

test_expect_success 'init prefers command line to GIT_DIR' '
	rm -rf newdir &&
	mkdir otherdir &&
	GIT_DIR=otherdir but --bare init newdir &&
	test_path_is_dir newdir/refs &&
	test_path_is_missing otherdir/refs
'

test_expect_success 'init with separate butdir' '
	rm -rf newdir &&
	but init --separate-but-dir realbutdir newdir &&
	newdir_but="$(cat newdir/.but)" &&
	test_cmp_fspath "$(pwd)/realbutdir" "${newdir_but#butdir: }" &&
	test_path_is_dir realbutdir/refs
'

test_expect_success 'explicit bare & --separate-but-dir incompatible' '
	test_must_fail but init --bare --separate-but-dir goop.but bare.but 2>err &&
	test_i18ngrep "cannot be used together" err
'

test_expect_success 'implicit bare & --separate-but-dir incompatible' '
	test_when_finished "rm -rf bare.but" &&
	mkdir -p bare.but &&
	test_must_fail env GIT_DIR=. \
		but -C bare.but init --separate-but-dir goop.but 2>err &&
	test_i18ngrep "incompatible" err
'

test_expect_success 'bare & --separate-but-dir incompatible within worktree' '
	test_when_finished "rm -rf bare.but linkwt seprepo" &&
	test_cummit gumby &&
	but clone --bare . bare.but &&
	but -C bare.but worktree add --detach ../linkwt &&
	test_must_fail but -C linkwt init --separate-but-dir seprepo 2>err &&
	test_i18ngrep "incompatible" err
'

test_lazy_prereq GETCWD_IGNORES_PERMS '
	base=GETCWD_TEST_BASE_DIR &&
	mkdir -p $base/dir &&
	chmod 100 $base ||
	BUG "cannot prepare $base"

	(
		cd $base/dir &&
		test-tool getcwd
	)
	status=$?

	chmod 700 $base &&
	rm -rf $base ||
	BUG "cannot clean $base"
	return $status
'

check_long_base_path () {
	# exceed initial buffer size of strbuf_getcwd()
	component=123456789abcdef &&
	test_when_finished "chmod 0700 $component; rm -rf $component" &&
	p31=$component/$component &&
	p127=$p31/$p31/$p31/$p31 &&
	mkdir -p $p127 &&
	if test $# = 1
	then
		chmod $1 $component
	fi &&
	(
		cd $p127 &&
		but init newdir
	)
}

test_expect_success 'init in long base path' '
	check_long_base_path
'

test_expect_success GETCWD_IGNORES_PERMS 'init in long restricted base path' '
	check_long_base_path 0111
'

test_expect_success 're-init on .but file' '
	( cd newdir && but init )
'

test_expect_success 're-init to update but link' '
	but -C newdir init --separate-but-dir ../surrealbutdir &&
	newdir_but="$(cat newdir/.but)" &&
	test_cmp_fspath "$(pwd)/surrealbutdir" "${newdir_but#butdir: }" &&
	test_path_is_dir surrealbutdir/refs &&
	test_path_is_missing realbutdir/refs
'

test_expect_success 're-init to move butdir' '
	rm -rf newdir realbutdir surrealbutdir &&
	but init newdir &&
	but -C newdir init --separate-but-dir ../realbutdir &&
	newdir_but="$(cat newdir/.but)" &&
	test_cmp_fspath "$(pwd)/realbutdir" "${newdir_but#butdir: }" &&
	test_path_is_dir realbutdir/refs
'

test_expect_success SYMLINKS 're-init to move butdir symlink' '
	rm -rf newdir realbutdir &&
	but init newdir &&
	(
	cd newdir &&
	mv .but here &&
	ln -s here .but &&
	but init --separate-but-dir ../realbutdir
	) &&
	echo "butdir: $(pwd)/realbutdir" >expected &&
	test_cmp expected newdir/.but &&
	test_cmp expected newdir/here &&
	test_path_is_dir realbutdir/refs
'

sep_but_dir_worktree ()  {
	test_when_finished "rm -rf mainwt linkwt seprepo" &&
	but init mainwt &&
	test_cummit -C mainwt gumby &&
	but -C mainwt worktree add --detach ../linkwt &&
	but -C "$1" init --separate-but-dir ../seprepo &&
	but -C mainwt rev-parse --but-common-dir >expect &&
	but -C linkwt rev-parse --but-common-dir >actual &&
	test_cmp expect actual
}

test_expect_success 're-init to move butdir with linked worktrees' '
	sep_but_dir_worktree mainwt
'

test_expect_success 're-init to move butdir within linked worktree' '
	sep_but_dir_worktree linkwt
'

test_expect_success MINGW '.but hidden' '
	rm -rf newdir &&
	(
		sane_unset GIT_DIR GIT_WORK_TREE &&
		mkdir newdir &&
		cd newdir &&
		but init &&
		test_path_is_hidden .but
	) &&
	check_config newdir/.but false unset
'

test_expect_success MINGW 'bare but dir not hidden' '
	rm -rf newdir &&
	(
		sane_unset GIT_DIR GIT_WORK_TREE GIT_CONFIG &&
		mkdir newdir &&
		cd newdir &&
		but --bare init
	) &&
	! is_hidden newdir
'

test_expect_success 'remote init from does not use config from cwd' '
	rm -rf newdir &&
	test_config core.logallrefupdates true &&
	but init newdir &&
	echo true >expect &&
	but -C newdir config --bool core.logallrefupdates >actual &&
	test_cmp expect actual
'

test_expect_success 're-init from a linked worktree' '
	but init main-worktree &&
	(
		cd main-worktree &&
		test_cummit first &&
		but worktree add ../linked-worktree &&
		mv .but/info/exclude expected-exclude &&
		cp .but/config expected-config &&
		find .but/worktrees -print | sort >expected &&
		but -C ../linked-worktree init &&
		test_cmp expected-exclude .but/info/exclude &&
		test_cmp expected-config .but/config &&
		find .but/worktrees -print | sort >actual &&
		test_cmp expected actual
	)
'

test_expect_success 'init honors GIT_DEFAULT_HASH' '
	GIT_DEFAULT_HASH=sha1 but init sha1 &&
	but -C sha1 rev-parse --show-object-format >actual &&
	echo sha1 >expected &&
	test_cmp expected actual &&
	GIT_DEFAULT_HASH=sha256 but init sha256 &&
	but -C sha256 rev-parse --show-object-format >actual &&
	echo sha256 >expected &&
	test_cmp expected actual
'

test_expect_success 'init honors --object-format' '
	but init --object-format=sha1 explicit-sha1 &&
	but -C explicit-sha1 rev-parse --show-object-format >actual &&
	echo sha1 >expected &&
	test_cmp expected actual &&
	but init --object-format=sha256 explicit-sha256 &&
	but -C explicit-sha256 rev-parse --show-object-format >actual &&
	echo sha256 >expected &&
	test_cmp expected actual
'

test_expect_success 'extensions.objectFormat is not allowed with repo version 0' '
	but init --object-format=sha256 explicit-v0 &&
	but -C explicit-v0 config core.repositoryformatversion 0 &&
	test_must_fail but -C explicit-v0 rev-parse --show-object-format
'

test_expect_success 'init rejects attempts to initialize with different hash' '
	test_must_fail but -C sha1 init --object-format=sha256 &&
	test_must_fail but -C sha256 init --object-format=sha1
'

test_expect_success MINGW 'core.hidedotfiles = false' '
	but config --global core.hidedotfiles false &&
	rm -rf newdir &&
	mkdir newdir &&
	(
		sane_unset GIT_DIR GIT_WORK_TREE GIT_CONFIG &&
		but -C newdir init
	) &&
	! is_hidden newdir/.but
'

test_expect_success MINGW 'redirect std handles' '
	GIT_REDIRECT_STDOUT=output.txt but rev-parse --but-dir &&
	test .but = "$(cat output.txt)" &&
	test -z "$(GIT_REDIRECT_STDOUT=off but rev-parse --but-dir)" &&
	test_must_fail env \
		GIT_REDIRECT_STDOUT=output.txt \
		GIT_REDIRECT_STDERR="2>&1" \
		but rev-parse --but-dir --verify refs/invalid &&
	grep "^\\.but\$" output.txt &&
	grep "Needed a single revision" output.txt
'

test_expect_success '--initial-branch' '
	but init --initial-branch=hello initial-branch-option &&
	but -C initial-branch-option symbolic-ref HEAD >actual &&
	echo refs/heads/hello >expect &&
	test_cmp expect actual &&

	: re-initializing should not change the branch name &&
	but init --initial-branch=ignore initial-branch-option 2>err &&
	test_i18ngrep "ignored --initial-branch" err &&
	but -C initial-branch-option symbolic-ref HEAD >actual &&
	grep hello actual
'

test_expect_success 'overridden default initial branch name (config)' '
	test_config_global init.defaultBranch nmb &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= but init initial-branch-config &&
	but -C initial-branch-config symbolic-ref HEAD >actual &&
	grep nmb actual
'

test_expect_success 'advice on unconfigured init.defaultBranch' '
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= but -c color.advice=always \
		init unconfigured-default-branch-name 2>err &&
	test_decode_color <err >decoded &&
	test_i18ngrep "<YELLOW>hint: " decoded
'

test_expect_success 'overridden default main branch name (env)' '
	test_config_global init.defaultBranch nmb &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=env but init main-branch-env &&
	but -C main-branch-env symbolic-ref HEAD >actual &&
	grep env actual
'

test_expect_success 'invalid default branch name' '
	test_must_fail env GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME="with space" \
		but init initial-branch-invalid 2>err &&
	test_i18ngrep "invalid branch name" err
'

test_expect_success 'branch -m with the initial branch' '
	but init rename-initial &&
	but -C rename-initial branch -m renamed &&
	test renamed = $(but -C rename-initial symbolic-ref --short HEAD) &&
	but -C rename-initial branch -m renamed again &&
	test again = $(but -C rename-initial symbolic-ref --short HEAD)
'

test_done
