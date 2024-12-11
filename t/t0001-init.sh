#!/bin/sh

test_description='git init'

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

	bare=$(cd "$1" && git config --bool core.bare)
	worktree=$(cd "$1" && git config core.worktree) ||
	worktree=unset

	test "$bare" = "$2" && test "$worktree" = "$3" || {
		echo "expected bare=$2 worktree=$3"
		echo "     got bare=$bare worktree=$worktree"
		return 1
	}
}

test_expect_success 'plain' '
	git init plain &&
	check_config plain/.git false unset
'

test_expect_success 'plain nested in bare' '
	(
		git init --bare bare-ancestor.git &&
		cd bare-ancestor.git &&
		mkdir plain-nested &&
		cd plain-nested &&
		git init
	) &&
	check_config bare-ancestor.git/plain-nested/.git false unset
'

test_expect_success 'plain through aliased command, outside any git repo' '
	(
		HOME=$(pwd)/alias-config &&
		export HOME &&
		mkdir alias-config &&
		echo "[alias] aliasedinit = init" >alias-config/.gitconfig &&

		GIT_CEILING_DIRECTORIES=$(pwd) &&
		export GIT_CEILING_DIRECTORIES &&

		mkdir plain-aliased &&
		cd plain-aliased &&
		git aliasedinit
	) &&
	check_config plain-aliased/.git false unset
'

test_expect_success 'plain nested through aliased command' '
	(
		git init plain-ancestor-aliased &&
		cd plain-ancestor-aliased &&
		echo "[alias] aliasedinit = init" >>.git/config &&
		mkdir plain-nested &&
		cd plain-nested &&
		git aliasedinit
	) &&
	check_config plain-ancestor-aliased/plain-nested/.git false unset
'

test_expect_success 'plain nested in bare through aliased command' '
	(
		git init --bare bare-ancestor-aliased.git &&
		cd bare-ancestor-aliased.git &&
		echo "[alias] aliasedinit = init" >>config &&
		mkdir plain-nested &&
		cd plain-nested &&
		git aliasedinit
	) &&
	check_config bare-ancestor-aliased.git/plain-nested/.git false unset
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
	git config alias.script \!./script &&
	( mkdir sub && cd sub && git script >../actual ) &&
	test_cmp expected actual
'

test_expect_success 'plain with GIT_WORK_TREE' '
	mkdir plain-wt &&
	test_must_fail env GIT_WORK_TREE="$(pwd)/plain-wt" git init plain-wt
'

test_expect_success 'plain bare' '
	git --bare init plain-bare-1 &&
	check_config plain-bare-1 true unset
'

test_expect_success 'plain bare with GIT_WORK_TREE' '
	mkdir plain-bare-2 &&
	test_must_fail \
		env GIT_WORK_TREE="$(pwd)/plain-bare-2" \
		git --bare init plain-bare-2
'

test_expect_success 'GIT_DIR bare' '
	mkdir git-dir-bare.git &&
	GIT_DIR=git-dir-bare.git git init &&
	check_config git-dir-bare.git true unset
'

test_expect_success 'init --bare' '
	git init --bare init-bare.git &&
	check_config init-bare.git true unset
'

test_expect_success 'GIT_DIR non-bare' '

	(
		mkdir non-bare &&
		cd non-bare &&
		GIT_DIR=.git git init
	) &&
	check_config non-bare/.git false unset
'

test_expect_success 'GIT_DIR & GIT_WORK_TREE (1)' '

	(
		mkdir git-dir-wt-1.git &&
		GIT_WORK_TREE=$(pwd) GIT_DIR=git-dir-wt-1.git git init
	) &&
	check_config git-dir-wt-1.git false "$(pwd)"
'

test_expect_success 'GIT_DIR & GIT_WORK_TREE (2)' '
	mkdir git-dir-wt-2.git &&
	test_must_fail env \
		GIT_WORK_TREE="$(pwd)" \
		GIT_DIR=git-dir-wt-2.git \
		git --bare init
'

test_expect_success 'reinit' '

	(
		mkdir again &&
		cd again &&
		git -c init.defaultBranch=initial init >out1 2>err1 &&
		git init >out2 2>err2
	) &&
	test_grep "Initialized empty" again/out1 &&
	test_grep "Reinitialized existing" again/out2 &&
	test_must_be_empty again/err1 &&
	test_must_be_empty again/err2
'

test_expect_success 'init with --template' '
	mkdir template-source &&
	echo content >template-source/file &&
	git init --template=template-source template-custom &&
	test_cmp template-source/file template-custom/.git/file
'

test_expect_success 'init with --template (blank)' '
	git init template-plain &&
	test_path_is_file template-plain/.git/info/exclude &&
	git init --template= template-blank &&
	test_path_is_missing template-blank/.git/info/exclude
'

init_no_templatedir_env () {
	(
		sane_unset GIT_TEMPLATE_DIR &&
		NO_SET_GIT_TEMPLATE_DIR=t &&
		export NO_SET_GIT_TEMPLATE_DIR &&
		git init "$1"
	)
}

test_expect_success 'init with init.templatedir set' '
	mkdir templatedir-source &&
	echo Content >templatedir-source/file &&
	test_config_global init.templatedir "${HOME}/templatedir-source" &&

	init_no_templatedir_env templatedir-set &&
	test_cmp templatedir-source/file templatedir-set/.git/file
'

test_expect_success 'init with init.templatedir using ~ expansion' '
	mkdir -p templatedir-source &&
	echo Content >templatedir-source/file &&
	test_config_global init.templatedir "~/templatedir-source" &&

	init_no_templatedir_env templatedir-expansion &&
	test_cmp templatedir-source/file templatedir-expansion/.git/file
'

test_expect_success 'init --bare/--shared overrides system/global config' '
	test_config_global core.bare false &&
	test_config_global core.sharedRepository 0640 &&
	git init --bare --shared=0666 init-bare-shared-override &&
	check_config init-bare-shared-override true unset &&
	test x0666 = \
	x$(git config -f init-bare-shared-override/config core.sharedRepository)
'

test_expect_success 'init honors global core.sharedRepository' '
	test_config_global core.sharedRepository 0666 &&
	git init shared-honor-global &&
	test x0666 = \
	x$(git config -f shared-honor-global/.git/config core.sharedRepository)
'

test_expect_success 'init allows insanely long --template' '
	git init --template=$(printf "x%09999dx" 1) test
'

test_expect_success 'init creates a new directory' '
	rm -fr newdir &&
	git init newdir &&
	test_path_is_dir newdir/.git/refs
'

test_expect_success 'init creates a new bare directory' '
	rm -fr newdir &&
	git init --bare newdir &&
	test_path_is_dir newdir/refs
'

test_expect_success 'init recreates a directory' '
	rm -fr newdir &&
	mkdir newdir &&
	git init newdir &&
	test_path_is_dir newdir/.git/refs
'

test_expect_success 'init recreates a new bare directory' '
	rm -fr newdir &&
	mkdir newdir &&
	git init --bare newdir &&
	test_path_is_dir newdir/refs
'

test_expect_success 'init creates a new deep directory' '
	rm -fr newdir &&
	git init newdir/a/b/c &&
	test_path_is_dir newdir/a/b/c/.git/refs
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
		git init --bare --shared=0660 newdir/a/b/c &&
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
	test_must_fail git init newdir &&
	test_path_is_file newdir
'

test_expect_success 'init notices EEXIST (2)' '
	rm -fr newdir &&
	mkdir newdir &&
	>newdir/a &&
	test_must_fail git init newdir/a/b &&
	test_path_is_file newdir/a
'

test_expect_success POSIXPERM,SANITY 'init notices EPERM' '
	test_when_finished "chmod +w newdir" &&
	rm -fr newdir &&
	mkdir newdir &&
	chmod -w newdir &&
	test_must_fail git init newdir/a/b
'

test_expect_success 'init creates a new bare directory with global --bare' '
	rm -rf newdir &&
	git --bare init newdir &&
	test_path_is_dir newdir/refs
'

test_expect_success 'init prefers command line to GIT_DIR' '
	rm -rf newdir &&
	mkdir otherdir &&
	GIT_DIR=otherdir git --bare init newdir &&
	test_path_is_dir newdir/refs &&
	test_path_is_missing otherdir/refs
'

test_expect_success 'init with separate gitdir' '
	rm -rf newdir &&
	git init --separate-git-dir realgitdir newdir &&
	newdir_git="$(cat newdir/.git)" &&
	test_cmp_fspath "$(pwd)/realgitdir" "${newdir_git#gitdir: }" &&
	test_path_is_dir realgitdir/refs
'

test_expect_success 'explicit bare & --separate-git-dir incompatible' '
	test_must_fail git init --bare --separate-git-dir goop.git bare.git 2>err &&
	test_grep "cannot be used together" err
'

test_expect_success 'implicit bare & --separate-git-dir incompatible' '
	test_when_finished "rm -rf bare.git" &&
	mkdir -p bare.git &&
	test_must_fail env GIT_DIR=. \
		git -C bare.git init --separate-git-dir goop.git 2>err &&
	test_grep "incompatible" err
'

test_expect_success 'bare & --separate-git-dir incompatible within worktree' '
	test_when_finished "rm -rf bare.git linkwt seprepo" &&
	test_commit gumby &&
	git clone --bare . bare.git &&
	git -C bare.git worktree add --detach ../linkwt &&
	test_must_fail git -C linkwt init --separate-git-dir seprepo 2>err &&
	test_grep "incompatible" err
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
		git init newdir
	)
}

test_expect_success 'init in long base path' '
	check_long_base_path
'

test_expect_success GETCWD_IGNORES_PERMS 'init in long restricted base path' '
	check_long_base_path 0111
'

test_expect_success 're-init on .git file' '
	( cd newdir && git init )
'

test_expect_success 're-init to update git link' '
	git -C newdir init --separate-git-dir ../surrealgitdir &&
	newdir_git="$(cat newdir/.git)" &&
	test_cmp_fspath "$(pwd)/surrealgitdir" "${newdir_git#gitdir: }" &&
	test_path_is_dir surrealgitdir/refs &&
	test_path_is_missing realgitdir/refs
'

test_expect_success 're-init to move gitdir' '
	rm -rf newdir realgitdir surrealgitdir &&
	git init newdir &&
	git -C newdir init --separate-git-dir ../realgitdir &&
	newdir_git="$(cat newdir/.git)" &&
	test_cmp_fspath "$(pwd)/realgitdir" "${newdir_git#gitdir: }" &&
	test_path_is_dir realgitdir/refs
'

test_expect_success SYMLINKS 're-init to move gitdir symlink' '
	rm -rf newdir realgitdir &&
	git init newdir &&
	(
	cd newdir &&
	mv .git here &&
	ln -s here .git &&
	git init --separate-git-dir ../realgitdir
	) &&
	echo "gitdir: $(pwd)/realgitdir" >expected &&
	test_cmp expected newdir/.git &&
	test_cmp expected newdir/here &&
	test_path_is_dir realgitdir/refs
'

sep_git_dir_worktree ()  {
	test_when_finished "rm -rf mainwt linkwt seprepo" &&
	git init mainwt &&
	if test "relative" = $2
	then
		test_config -C mainwt worktree.useRelativePaths true
	else
		test_config -C mainwt worktree.useRelativePaths false
	fi
	test_commit -C mainwt gumby &&
	git -C mainwt worktree add --detach ../linkwt &&
	git -C "$1" init --separate-git-dir ../seprepo &&
	git -C mainwt rev-parse --git-common-dir >expect &&
	git -C linkwt rev-parse --git-common-dir >actual &&
	test_cmp expect actual
}

test_expect_success 're-init to move gitdir with linked worktrees (absolute)' '
	sep_git_dir_worktree mainwt absolute
'

test_expect_success 're-init to move gitdir within linked worktree (absolute)' '
	sep_git_dir_worktree linkwt absolute
'

test_expect_success 're-init to move gitdir with linked worktrees (relative)' '
	sep_git_dir_worktree mainwt relative
'

test_expect_success 're-init to move gitdir within linked worktree (relative)' '
	sep_git_dir_worktree linkwt relative
'

test_expect_success MINGW '.git hidden' '
	rm -rf newdir &&
	(
		sane_unset GIT_DIR GIT_WORK_TREE &&
		mkdir newdir &&
		cd newdir &&
		git init &&
		test_path_is_hidden .git
	) &&
	check_config newdir/.git false unset
'

test_expect_success MINGW 'bare git dir not hidden' '
	rm -rf newdir &&
	(
		sane_unset GIT_DIR GIT_WORK_TREE GIT_CONFIG &&
		mkdir newdir &&
		cd newdir &&
		git --bare init
	) &&
	! is_hidden newdir
'

test_expect_success 'remote init from does not use config from cwd' '
	rm -rf newdir &&
	test_config core.logallrefupdates true &&
	git init newdir &&
	echo true >expect &&
	git -C newdir config --bool core.logallrefupdates >actual &&
	test_cmp expect actual
'

test_expect_success 're-init from a linked worktree' '
	git init main-worktree &&
	(
		cd main-worktree &&
		test_commit first &&
		git worktree add ../linked-worktree &&
		mv .git/info/exclude expected-exclude &&
		cp .git/config expected-config &&
		find .git/worktrees -print | sort >expected &&
		git -C ../linked-worktree init &&
		test_cmp expected-exclude .git/info/exclude &&
		test_cmp expected-config .git/config &&
		find .git/worktrees -print | sort >actual &&
		test_cmp expected actual
	)
'

test_expect_success 'init honors GIT_DEFAULT_HASH' '
	test_when_finished "rm -rf sha1 sha256" &&
	GIT_DEFAULT_HASH=sha1 git init sha1 &&
	git -C sha1 rev-parse --show-object-format >actual &&
	echo sha1 >expected &&
	test_cmp expected actual &&
	GIT_DEFAULT_HASH=sha256 git init sha256 &&
	git -C sha256 rev-parse --show-object-format >actual &&
	echo sha256 >expected &&
	test_cmp expected actual
'

test_expect_success 'init honors --object-format' '
	test_when_finished "rm -rf explicit-sha1 explicit-sha256" &&
	git init --object-format=sha1 explicit-sha1 &&
	git -C explicit-sha1 rev-parse --show-object-format >actual &&
	echo sha1 >expected &&
	test_cmp expected actual &&
	git init --object-format=sha256 explicit-sha256 &&
	git -C explicit-sha256 rev-parse --show-object-format >actual &&
	echo sha256 >expected &&
	test_cmp expected actual
'

test_expect_success 'init honors init.defaultObjectFormat' '
	test_when_finished "rm -rf sha1 sha256" &&

	test_config_global init.defaultObjectFormat sha1 &&
	(
		sane_unset GIT_DEFAULT_HASH &&
		git init sha1 &&
		git -C sha1 rev-parse --show-object-format >actual &&
		echo sha1 >expected &&
		test_cmp expected actual
	) &&

	test_config_global init.defaultObjectFormat sha256 &&
	(
		sane_unset GIT_DEFAULT_HASH &&
		git init sha256 &&
		git -C sha256 rev-parse --show-object-format >actual &&
		echo sha256 >expected &&
		test_cmp expected actual
	)
'

test_expect_success 'init warns about invalid init.defaultObjectFormat' '
	test_when_finished "rm -rf repo" &&
	test_config_global init.defaultObjectFormat garbage &&

	echo "warning: unknown hash algorithm ${SQ}garbage${SQ}" >expect &&
	git init repo 2>err &&
	test_cmp expect err &&

	git -C repo rev-parse --show-object-format >actual &&
	echo $GIT_DEFAULT_HASH >expected &&
	test_cmp expected actual
'

test_expect_success '--object-format overrides GIT_DEFAULT_HASH' '
	test_when_finished "rm -rf repo" &&
	GIT_DEFAULT_HASH=sha1 git init --object-format=sha256 repo &&
	git -C repo rev-parse --show-object-format >actual &&
	echo sha256 >expected
'

test_expect_success 'GIT_DEFAULT_HASH overrides init.defaultObjectFormat' '
	test_when_finished "rm -rf repo" &&
	test_config_global init.defaultObjectFormat sha1 &&
	GIT_DEFAULT_HASH=sha256 git init repo &&
	git -C repo rev-parse --show-object-format >actual &&
	echo sha256 >expected
'

test_expect_success 'extensions.objectFormat is not allowed with repo version 0' '
	test_when_finished "rm -rf explicit-v0" &&
	git init --object-format=sha256 explicit-v0 &&
	git -C explicit-v0 config core.repositoryformatversion 0 &&
	test_must_fail git -C explicit-v0 rev-parse --show-object-format
'

test_expect_success 'init rejects attempts to initialize with different hash' '
	test_must_fail git -C sha1 init --object-format=sha256 &&
	test_must_fail git -C sha256 init --object-format=sha1
'

test_expect_success DEFAULT_REPO_FORMAT 'extensions.refStorage is not allowed with repo version 0' '
	test_when_finished "rm -rf refstorage" &&
	git init refstorage &&
	git -C refstorage config extensions.refStorage files &&
	test_must_fail git -C refstorage rev-parse 2>err &&
	grep "repo version is 0, but v1-only extension found" err
'

test_expect_success DEFAULT_REPO_FORMAT 'extensions.refStorage with files backend' '
	test_when_finished "rm -rf refstorage" &&
	git init refstorage &&
	git -C refstorage config core.repositoryformatversion 1 &&
	git -C refstorage config extensions.refStorage files &&
	test_commit -C refstorage A &&
	git -C refstorage rev-parse --verify HEAD
'

test_expect_success DEFAULT_REPO_FORMAT 'extensions.refStorage with unknown backend' '
	test_when_finished "rm -rf refstorage" &&
	git init refstorage &&
	git -C refstorage config core.repositoryformatversion 1 &&
	git -C refstorage config extensions.refStorage garbage &&
	test_must_fail git -C refstorage rev-parse 2>err &&
	grep "invalid value for ${SQ}extensions.refstorage${SQ}: ${SQ}garbage${SQ}" err
'

test_expect_success 'init with GIT_DEFAULT_REF_FORMAT=garbage' '
	test_when_finished "rm -rf refformat" &&
	cat >expect <<-EOF &&
	fatal: unknown ref storage format ${SQ}garbage${SQ}
	EOF
	test_must_fail env GIT_DEFAULT_REF_FORMAT=garbage git init refformat 2>err &&
	test_cmp expect err
'

test_expect_success 'init warns about invalid init.defaultRefFormat' '
	test_when_finished "rm -rf repo" &&
	test_config_global init.defaultRefFormat garbage &&

	echo "warning: unknown ref storage format ${SQ}garbage${SQ}" >expect &&
	git init repo 2>err &&
	test_cmp expect err &&

	git -C repo rev-parse --show-ref-format >actual &&
	echo $GIT_DEFAULT_REF_FORMAT >expected &&
	test_cmp expected actual
'

backends="files reftable"
for format in $backends
do
	test_expect_success DEFAULT_REPO_FORMAT "init with GIT_DEFAULT_REF_FORMAT=$format" '
		test_when_finished "rm -rf refformat" &&
		GIT_DEFAULT_REF_FORMAT=$format git init refformat &&

		if test $format = files
		then
			test_must_fail git -C refformat config extensions.refstorage &&
			echo 0 >expect
		else
			git -C refformat config extensions.refstorage &&
			echo 1 >expect
		fi &&
		git -C refformat config core.repositoryformatversion >actual &&
		test_cmp expect actual &&

		echo $format >expect &&
		git -C refformat rev-parse --show-ref-format >actual &&
		test_cmp expect actual
	'

	test_expect_success "init with --ref-format=$format" '
		test_when_finished "rm -rf refformat" &&
		git init --ref-format=$format refformat &&
		echo $format >expect &&
		git -C refformat rev-parse --show-ref-format >actual &&
		test_cmp expect actual
	'

	test_expect_success "init with init.defaultRefFormat=$format" '
		test_when_finished "rm -rf refformat" &&
		test_config_global init.defaultRefFormat $format &&
		(
			sane_unset GIT_DEFAULT_REF_FORMAT &&
			git init refformat
		) &&

		echo $format >expect &&
		git -C refformat rev-parse --show-ref-format >actual &&
		test_cmp expect actual
	'

	test_expect_success "--ref-format=$format overrides GIT_DEFAULT_REF_FORMAT" '
		test_when_finished "rm -rf refformat" &&
		GIT_DEFAULT_REF_FORMAT=garbage git init --ref-format=$format refformat &&
		echo $format >expect &&
		git -C refformat rev-parse --show-ref-format >actual &&
		test_cmp expect actual
	'
done

test_expect_success "--ref-format= overrides GIT_DEFAULT_REF_FORMAT" '
	test_when_finished "rm -rf refformat" &&
	GIT_DEFAULT_REF_FORMAT=files git init --ref-format=reftable refformat &&
	echo reftable >expect &&
	git -C refformat rev-parse --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success "GIT_DEFAULT_REF_FORMAT= overrides init.defaultRefFormat" '
	test_when_finished "rm -rf refformat" &&
	test_config_global init.defaultRefFormat files &&

	GIT_DEFAULT_REF_FORMAT=reftable git init refformat &&
	echo reftable >expect &&
	git -C refformat rev-parse --show-ref-format >actual &&
	test_cmp expect actual
'

for from_format in $backends
do
	test_expect_success "re-init with same format ($from_format)" '
		test_when_finished "rm -rf refformat" &&
		git init --ref-format=$from_format refformat &&
		git init --ref-format=$from_format refformat &&
		echo $from_format >expect &&
		git -C refformat rev-parse --show-ref-format >actual &&
		test_cmp expect actual
	'

	for to_format in $backends
	do
		if test "$from_format" = "$to_format"
		then
			continue
		fi

		test_expect_success "re-init with different format fails ($from_format -> $to_format)" '
			test_when_finished "rm -rf refformat" &&
			git init --ref-format=$from_format refformat &&
			cat >expect <<-EOF &&
			fatal: attempt to reinitialize repository with different reference storage format
			EOF
			test_must_fail git init --ref-format=$to_format refformat 2>err &&
			test_cmp expect err &&
			echo $from_format >expect &&
			git -C refformat rev-parse --show-ref-format >actual &&
			test_cmp expect actual
		'
	done
done

test_expect_success 'init with --ref-format=garbage' '
	test_when_finished "rm -rf refformat" &&
	cat >expect <<-EOF &&
	fatal: unknown ref storage format ${SQ}garbage${SQ}
	EOF
	test_must_fail git init --ref-format=garbage refformat 2>err &&
	test_cmp expect err
'

test_expect_success MINGW 'core.hidedotfiles = false' '
	git config --global core.hidedotfiles false &&
	rm -rf newdir &&
	mkdir newdir &&
	(
		sane_unset GIT_DIR GIT_WORK_TREE GIT_CONFIG &&
		git -C newdir init
	) &&
	! is_hidden newdir/.git
'

test_expect_success MINGW 'redirect std handles' '
	GIT_REDIRECT_STDOUT=output.txt git rev-parse --git-dir &&
	test .git = "$(cat output.txt)" &&
	test -z "$(GIT_REDIRECT_STDOUT=off git rev-parse --git-dir)" &&
	test_must_fail env \
		GIT_REDIRECT_STDOUT=output.txt \
		GIT_REDIRECT_STDERR="2>&1" \
		git rev-parse --git-dir --verify refs/invalid &&
	grep "^\\.git\$" output.txt &&
	grep "Needed a single revision" output.txt
'

test_expect_success '--initial-branch' '
	git init --initial-branch=hello initial-branch-option &&
	git -C initial-branch-option symbolic-ref HEAD >actual &&
	echo refs/heads/hello >expect &&
	test_cmp expect actual &&

	: re-initializing should not change the branch name &&
	git init --initial-branch=ignore initial-branch-option 2>err &&
	test_grep "ignored --initial-branch" err &&
	git -C initial-branch-option symbolic-ref HEAD >actual &&
	grep hello actual
'

test_expect_success 'overridden default initial branch name (config)' '
	test_config_global init.defaultBranch nmb &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= git init initial-branch-config &&
	git -C initial-branch-config symbolic-ref HEAD >actual &&
	grep nmb actual
'

test_expect_success 'advice on unconfigured init.defaultBranch' '
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME= git -c color.advice=always \
		init unconfigured-default-branch-name 2>err &&
	test_decode_color <err >decoded &&
	test_grep "<YELLOW>hint: " decoded
'

test_expect_success 'overridden default main branch name (env)' '
	test_config_global init.defaultBranch nmb &&
	GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=env git init main-branch-env &&
	git -C main-branch-env symbolic-ref HEAD >actual &&
	grep env actual
'

test_expect_success 'invalid default branch name' '
	test_must_fail env GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME="with space" \
		git init initial-branch-invalid 2>err &&
	test_grep "invalid branch name" err
'

test_expect_success 'branch -m with the initial branch' '
	git init rename-initial &&
	git -C rename-initial branch -m renamed &&
	echo renamed >expect &&
	git -C rename-initial symbolic-ref --short HEAD >actual &&
	test_cmp expect actual &&

	git -C rename-initial branch -m renamed again &&
	echo again >expect &&
	git -C rename-initial symbolic-ref --short HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'init with includeIf.onbranch condition' '
	test_when_finished "rm -rf repo" &&
	git -c includeIf.onbranch:main.path=nonexistent init repo &&
	echo $GIT_DEFAULT_REF_FORMAT >expect &&
	git -C repo rev-parse --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success 'init with includeIf.onbranch condition with existing directory' '
	test_when_finished "rm -rf repo" &&
	mkdir repo &&
	git -c includeIf.onbranch:nonexistent.path=/does/not/exist init repo &&
	echo $GIT_DEFAULT_REF_FORMAT >expect &&
	git -C repo rev-parse --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success 're-init with includeIf.onbranch condition' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	git -c includeIf.onbranch:nonexistent.path=/does/not/exist init repo &&
	echo $GIT_DEFAULT_REF_FORMAT >expect &&
	git -C repo rev-parse --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success 're-init with includeIf.onbranch condition' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	git -c includeIf.onbranch:nonexistent.path=/does/not/exist init repo &&
	echo $GIT_DEFAULT_REF_FORMAT >expect &&
	git -C repo rev-parse --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success 're-init skips non-matching includeIf.onbranch' '
	test_when_finished "rm -rf repo config" &&
	cat >config <<-EOF &&
	[
	garbage
	EOF
	git init repo &&
	git -c includeIf.onbranch:nonexistent.path="$(test-tool path-utils absolute_path config)" init repo
'

test_expect_success 're-init reads matching includeIf.onbranch' '
	test_when_finished "rm -rf repo config" &&
	cat >config <<-EOF &&
	[
	garbage
	EOF
	path="$(test-tool path-utils absolute_path config)" &&
	git init --initial-branch=branch repo &&
	cat >expect <<-EOF &&
	fatal: bad config line 1 in file $path
	EOF
	test_must_fail git -c includeIf.onbranch:branch.path="$path" init repo 2>err &&
	test_cmp expect err
'

test_done
