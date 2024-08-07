#!/bin/sh

test_description='test git rev-parse'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_one () {
	dir="$1" &&
	expect="$2" &&
	shift &&
	shift &&
	echo "$expect" >expect &&
	git -C "$dir" rev-parse "$@" >actual &&
	test_cmp expect actual
}

# usage: [options] label is-bare is-inside-git is-inside-work prefix git-dir absolute-git-dir
test_rev_parse () {
	d=
	bare=
	gitdir=
	while :
	do
		case "$1" in
		-C) d="$2"; shift; shift ;;
		-b) case "$2" in
		    [tfu]*) bare="$2"; shift; shift ;;
		    *) error "test_rev_parse: bogus core.bare value '$2'" ;;
		    esac ;;
		-g) gitdir="$2"; shift; shift ;;
		-*) error "test_rev_parse: unrecognized option '$1'" ;;
		*) break ;;
		esac
	done

	name=$1
	shift

	for o in --is-bare-repository \
		 --is-inside-git-dir \
		 --is-inside-work-tree \
		 --show-prefix \
		 --git-dir \
		 --absolute-git-dir
	do
		test $# -eq 0 && break
		expect="$1"
		test_expect_success "$name: $o" '
			if test -n "$gitdir"
			then
				test_when_finished "unset GIT_DIR" &&
				GIT_DIR="$gitdir" &&
				export GIT_DIR
			fi &&

			case "$bare" in
			t*) test_config ${d:+-C} ${d:+"$d"} core.bare true ;;
			f*) test_config ${d:+-C} ${d:+"$d"} core.bare false ;;
			u*) test_unconfig ${d:+-C} ${d:+"$d"} core.bare ;;
			esac &&

			echo "$expect" >expect &&
			git ${d:+-C} ${d:+"$d"} rev-parse $o >actual &&
			test_cmp expect actual
		'
		shift
	done
}

ROOT=$(pwd)

test_expect_success 'setup' '
	mkdir -p sub/dir work &&
	cp -R .git repo.git &&
	git checkout -B main &&
	test_commit abc &&
	git checkout -b side &&
	test_commit def &&
	git checkout main &&
	git worktree add worktree side
'

test_rev_parse toplevel false false true '' .git "$ROOT/.git"

test_rev_parse -C .git .git/ false true false '' . "$ROOT/.git"
test_rev_parse -C .git/objects .git/objects/ false true false '' "$ROOT/.git" "$ROOT/.git"

test_rev_parse -C sub/dir subdirectory false false true sub/dir/ "$ROOT/.git" "$ROOT/.git"

test_rev_parse -b t 'core.bare = true' true false false

test_rev_parse -b u 'core.bare undefined' false false true


test_rev_parse -C work -g ../.git -b f 'GIT_DIR=../.git, core.bare = false' false false true '' "../.git" "$ROOT/.git"

test_rev_parse -C work -g ../.git -b t 'GIT_DIR=../.git, core.bare = true' true false false ''

test_rev_parse -C work -g ../.git -b u 'GIT_DIR=../.git, core.bare undefined' false false true ''


test_rev_parse -C work -g ../repo.git -b f 'GIT_DIR=../repo.git, core.bare = false' false false true '' "../repo.git" "$ROOT/repo.git"

test_rev_parse -C work -g ../repo.git -b t 'GIT_DIR=../repo.git, core.bare = true' true false false ''

test_rev_parse -C work -g ../repo.git -b u 'GIT_DIR=../repo.git, core.bare undefined' false false true ''

test_expect_success 'rev-parse --path-format=absolute' '
	test_one "." "$ROOT/.git" --path-format=absolute --git-dir &&
	test_one "." "$ROOT/.git" --path-format=absolute --git-common-dir &&
	test_one "sub/dir" "$ROOT/.git" --path-format=absolute --git-dir &&
	test_one "sub/dir" "$ROOT/.git" --path-format=absolute --git-common-dir &&
	test_one "worktree" "$ROOT/.git/worktrees/worktree" --path-format=absolute --git-dir &&
	test_one "worktree" "$ROOT/.git" --path-format=absolute --git-common-dir &&
	test_one "." "$ROOT" --path-format=absolute --show-toplevel &&
	test_one "." "$ROOT/.git/objects" --path-format=absolute --git-path objects &&
	test_one "." "$ROOT/.git/objects/foo/bar/baz" --path-format=absolute --git-path objects/foo/bar/baz
'

test_expect_success 'rev-parse --path-format=relative' '
	test_one "." ".git" --path-format=relative --git-dir &&
	test_one "." ".git" --path-format=relative --git-common-dir &&
	test_one "sub/dir" "../../.git" --path-format=relative --git-dir &&
	test_one "sub/dir" "../../.git" --path-format=relative --git-common-dir &&
	test_one "worktree" "../.git/worktrees/worktree" --path-format=relative --git-dir &&
	test_one "worktree" "../.git" --path-format=relative --git-common-dir &&
	test_one "." "./" --path-format=relative --show-toplevel &&
	test_one "." ".git/objects" --path-format=relative --git-path objects &&
	test_one "." ".git/objects/foo/bar/baz" --path-format=relative --git-path objects/foo/bar/baz
'

test_expect_success '--path-format=relative does not affect --absolute-git-dir' '
	git rev-parse --path-format=relative --absolute-git-dir >actual &&
	echo "$ROOT/.git" >expect &&
	test_cmp expect actual
'

test_expect_success '--path-format can change in the middle of the command line' '
	git rev-parse --path-format=absolute --git-dir --path-format=relative --git-path objects/foo/bar >actual &&
	cat >expect <<-EOF &&
	$ROOT/.git
	.git/objects/foo/bar
	EOF
	test_cmp expect actual
'

test_expect_success '--path-format does not segfault without an argument' '
	test_must_fail git rev-parse --path-format
'

test_expect_success 'git-common-dir from worktree root' '
	echo .git >expect &&
	git rev-parse --git-common-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'git-common-dir inside sub-dir' '
	mkdir -p path/to/child &&
	test_when_finished "rm -rf path" &&
	echo "$(git -C path/to/child rev-parse --show-cdup).git" >expect &&
	git -C path/to/child rev-parse --git-common-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'git-path from worktree root' '
	echo .git/objects >expect &&
	git rev-parse --git-path objects >actual &&
	test_cmp expect actual
'

test_expect_success 'git-path inside sub-dir' '
	mkdir -p path/to/child &&
	test_when_finished "rm -rf path" &&
	echo "$(git -C path/to/child rev-parse --show-cdup).git/objects" >expect &&
	git -C path/to/child rev-parse --git-path objects >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --is-shallow-repository in shallow repo' '
	test_commit test_commit &&
	echo true >expect &&
	git clone --depth 1 --no-local . shallow &&
	test_when_finished "rm -rf shallow" &&
	git -C shallow rev-parse --is-shallow-repository >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --is-shallow-repository in non-shallow repo' '
	echo false >expect &&
	git rev-parse --is-shallow-repository >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --show-object-format in repo' '
	test_oid algo >expect &&
	git rev-parse --show-object-format >actual &&
	test_cmp expect actual &&
	git rev-parse --show-object-format=storage >actual &&
	test_cmp expect actual &&
	git rev-parse --show-object-format=input >actual &&
	test_cmp expect actual &&
	git rev-parse --show-object-format=output >actual &&
	test_cmp expect actual &&
	test_must_fail git rev-parse --show-object-format=squeamish-ossifrage 2>err &&
	grep "unknown mode for --show-object-format: squeamish-ossifrage" err
'

test_expect_success 'rev-parse --show-ref-format' '
	test_detect_ref_format >expect &&
	git rev-parse --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --show-ref-format with invalid storage' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git config extensions.refstorage broken &&
		test_must_fail git rev-parse --show-ref-format 2>err &&
		grep "error: invalid value for ${SQ}extensions.refstorage${SQ}: ${SQ}broken${SQ}" err
	)
'

test_expect_success '--show-toplevel from subdir of working tree' '
	pwd >expect &&
	git -C sub/dir rev-parse --show-toplevel >actual &&
	test_cmp expect actual
'

test_expect_success '--show-toplevel from inside .git' '
	test_must_fail git -C .git rev-parse --show-toplevel
'

test_expect_success 'showing the superproject correctly' '
	git rev-parse --show-superproject-working-tree >out &&
	test_must_be_empty out &&

	test_create_repo super &&
	test_commit -C super test_commit &&
	test_create_repo sub &&
	test_commit -C sub test_commit &&
	git -c protocol.file.allow=always \
		-C super submodule add ../sub dir/sub &&
	echo $(pwd)/super >expect  &&
	git -C super/dir/sub rev-parse --show-superproject-working-tree >out &&
	test_cmp expect out &&

	test_commit -C super submodule_add &&
	git -C super checkout -b branch1 &&
	git -C super/dir/sub checkout -b branch1 &&
	test_commit -C super/dir/sub branch1_commit &&
	git -C super add dir/sub &&
	test_commit -C super branch1_commit &&
	git -C super checkout -b branch2 main &&
	git -C super/dir/sub checkout -b branch2 main &&
	test_commit -C super/dir/sub branch2_commit &&
	git -C super add dir/sub &&
	test_commit -C super branch2_commit &&
	test_must_fail git -C super merge branch1 &&

	git -C super/dir/sub rev-parse --show-superproject-working-tree >out &&
	test_cmp expect out
'

# at least one external project depends on this behavior:
test_expect_success 'rev-parse --since= unsqueezed ordering' '
	x1=--since=1970-01-01T00:00:01Z &&
	x2=--since=1970-01-01T00:00:02Z &&
	x3=--since=1970-01-01T00:00:03Z &&
	git rev-parse $x1 $x1 $x3 $x2 >actual &&
	cat >expect <<-EOF &&
	--max-age=1
	--max-age=1
	--max-age=3
	--max-age=2
	EOF
	test_cmp expect actual
'

test_expect_success 'rev-parse --bisect includes bad, excludes good' '
	test_commit_bulk 6 &&

	git update-ref refs/bisect/bad-1 HEAD~1 &&
	git update-ref refs/bisect/b HEAD~2 &&
	git update-ref refs/bisect/bad-3 HEAD~3 &&
	git update-ref refs/bisect/good-3 HEAD~3 &&
	git update-ref refs/bisect/bad-4 HEAD~4 &&
	git update-ref refs/bisect/go HEAD~4 &&

	# Note: refs/bisect/b and refs/bisect/go should be ignored because they
	# do not match the refs/bisect/bad or refs/bisect/good prefixes.
	cat >expect <<-EOF &&
	refs/bisect/bad-1
	refs/bisect/bad-3
	refs/bisect/bad-4
	^refs/bisect/good-3
	EOF

	git rev-parse --symbolic-full-name --bisect >actual &&
	test_cmp expect actual
'

test_expect_success '--short= truncates to the actual hash length' '
	git rev-parse HEAD >expect &&
	git rev-parse --short=100 HEAD >actual &&
	test_cmp expect actual
'

test_done
