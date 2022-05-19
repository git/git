#!/bin/sh

test_description='test but rev-parse'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_one () {
	dir="$1" &&
	expect="$2" &&
	shift &&
	shift &&
	echo "$expect" >expect &&
	but -C "$dir" rev-parse "$@" >actual &&
	test_cmp expect actual
}

# usage: [options] label is-bare is-inside-but is-inside-work prefix but-dir absolute-but-dir
test_rev_parse () {
	d=
	bare=
	butdir=
	while :
	do
		case "$1" in
		-C) d="$2"; shift; shift ;;
		-b) case "$2" in
		    [tfu]*) bare="$2"; shift; shift ;;
		    *) error "test_rev_parse: bogus core.bare value '$2'" ;;
		    esac ;;
		-g) butdir="$2"; shift; shift ;;
		-*) error "test_rev_parse: unrecognized option '$1'" ;;
		*) break ;;
		esac
	done

	name=$1
	shift

	for o in --is-bare-repository \
		 --is-inside-but-dir \
		 --is-inside-work-tree \
		 --show-prefix \
		 --but-dir \
		 --absolute-but-dir
	do
		test $# -eq 0 && break
		expect="$1"
		test_expect_success "$name: $o" '
			if test -n "$butdir"
			then
				test_when_finished "unset BUT_DIR" &&
				BUT_DIR="$butdir" &&
				export BUT_DIR
			fi &&

			case "$bare" in
			t*) test_config ${d:+-C} ${d:+"$d"} core.bare true ;;
			f*) test_config ${d:+-C} ${d:+"$d"} core.bare false ;;
			u*) test_unconfig ${d:+-C} ${d:+"$d"} core.bare ;;
			esac &&

			echo "$expect" >expect &&
			but ${d:+-C} ${d:+"$d"} rev-parse $o >actual &&
			test_cmp expect actual
		'
		shift
	done
}

ROOT=$(pwd)

test_expect_success 'setup' '
	mkdir -p sub/dir work &&
	cp -R .but repo.but &&
	but checkout -B main &&
	test_cummit abc &&
	but checkout -b side &&
	test_cummit def &&
	but checkout main &&
	but worktree add worktree side
'

test_rev_parse toplevel false false true '' .but "$ROOT/.but"

test_rev_parse -C .but .but/ false true false '' . "$ROOT/.but"
test_rev_parse -C .but/objects .but/objects/ false true false '' "$ROOT/.but" "$ROOT/.but"

test_rev_parse -C sub/dir subdirectory false false true sub/dir/ "$ROOT/.but" "$ROOT/.but"

test_rev_parse -b t 'core.bare = true' true false false

test_rev_parse -b u 'core.bare undefined' false false true


test_rev_parse -C work -g ../.but -b f 'BUT_DIR=../.but, core.bare = false' false false true '' "../.but" "$ROOT/.but"

test_rev_parse -C work -g ../.but -b t 'BUT_DIR=../.but, core.bare = true' true false false ''

test_rev_parse -C work -g ../.but -b u 'BUT_DIR=../.but, core.bare undefined' false false true ''


test_rev_parse -C work -g ../repo.but -b f 'BUT_DIR=../repo.but, core.bare = false' false false true '' "../repo.but" "$ROOT/repo.but"

test_rev_parse -C work -g ../repo.but -b t 'BUT_DIR=../repo.but, core.bare = true' true false false ''

test_rev_parse -C work -g ../repo.but -b u 'BUT_DIR=../repo.but, core.bare undefined' false false true ''

test_expect_success 'rev-parse --path-format=absolute' '
	test_one "." "$ROOT/.but" --path-format=absolute --but-dir &&
	test_one "." "$ROOT/.but" --path-format=absolute --but-common-dir &&
	test_one "sub/dir" "$ROOT/.but" --path-format=absolute --but-dir &&
	test_one "sub/dir" "$ROOT/.but" --path-format=absolute --but-common-dir &&
	test_one "worktree" "$ROOT/.but/worktrees/worktree" --path-format=absolute --but-dir &&
	test_one "worktree" "$ROOT/.but" --path-format=absolute --but-common-dir &&
	test_one "." "$ROOT" --path-format=absolute --show-toplevel &&
	test_one "." "$ROOT/.but/objects" --path-format=absolute --but-path objects &&
	test_one "." "$ROOT/.but/objects/foo/bar/baz" --path-format=absolute --but-path objects/foo/bar/baz
'

test_expect_success 'rev-parse --path-format=relative' '
	test_one "." ".but" --path-format=relative --but-dir &&
	test_one "." ".but" --path-format=relative --but-common-dir &&
	test_one "sub/dir" "../../.but" --path-format=relative --but-dir &&
	test_one "sub/dir" "../../.but" --path-format=relative --but-common-dir &&
	test_one "worktree" "../.but/worktrees/worktree" --path-format=relative --but-dir &&
	test_one "worktree" "../.but" --path-format=relative --but-common-dir &&
	test_one "." "./" --path-format=relative --show-toplevel &&
	test_one "." ".but/objects" --path-format=relative --but-path objects &&
	test_one "." ".but/objects/foo/bar/baz" --path-format=relative --but-path objects/foo/bar/baz
'

test_expect_success '--path-format=relative does not affect --absolute-but-dir' '
	but rev-parse --path-format=relative --absolute-but-dir >actual &&
	echo "$ROOT/.but" >expect &&
	test_cmp expect actual
'

test_expect_success '--path-format can change in the middle of the command line' '
	but rev-parse --path-format=absolute --but-dir --path-format=relative --but-path objects/foo/bar >actual &&
	cat >expect <<-EOF &&
	$ROOT/.but
	.but/objects/foo/bar
	EOF
	test_cmp expect actual
'

test_expect_success '--path-format does not segfault without an argument' '
	test_must_fail but rev-parse --path-format
'

test_expect_success 'but-common-dir from worktree root' '
	echo .but >expect &&
	but rev-parse --but-common-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'but-common-dir inside sub-dir' '
	mkdir -p path/to/child &&
	test_when_finished "rm -rf path" &&
	echo "$(but -C path/to/child rev-parse --show-cdup).but" >expect &&
	but -C path/to/child rev-parse --but-common-dir >actual &&
	test_cmp expect actual
'

test_expect_success 'but-path from worktree root' '
	echo .but/objects >expect &&
	but rev-parse --but-path objects >actual &&
	test_cmp expect actual
'

test_expect_success 'but-path inside sub-dir' '
	mkdir -p path/to/child &&
	test_when_finished "rm -rf path" &&
	echo "$(but -C path/to/child rev-parse --show-cdup).but/objects" >expect &&
	but -C path/to/child rev-parse --but-path objects >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --is-shallow-repository in shallow repo' '
	test_cummit test_cummit &&
	echo true >expect &&
	but clone --depth 1 --no-local . shallow &&
	test_when_finished "rm -rf shallow" &&
	but -C shallow rev-parse --is-shallow-repository >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --is-shallow-repository in non-shallow repo' '
	echo false >expect &&
	but rev-parse --is-shallow-repository >actual &&
	test_cmp expect actual
'

test_expect_success 'rev-parse --show-object-format in repo' '
	echo "$(test_oid algo)" >expect &&
	but rev-parse --show-object-format >actual &&
	test_cmp expect actual &&
	but rev-parse --show-object-format=storage >actual &&
	test_cmp expect actual &&
	but rev-parse --show-object-format=input >actual &&
	test_cmp expect actual &&
	but rev-parse --show-object-format=output >actual &&
	test_cmp expect actual &&
	test_must_fail but rev-parse --show-object-format=squeamish-ossifrage 2>err &&
	grep "unknown mode for --show-object-format: squeamish-ossifrage" err
'

test_expect_success '--show-toplevel from subdir of working tree' '
	pwd >expect &&
	but -C sub/dir rev-parse --show-toplevel >actual &&
	test_cmp expect actual
'

test_expect_success '--show-toplevel from inside .but' '
	test_must_fail but -C .but rev-parse --show-toplevel
'

test_expect_success 'showing the superproject correctly' '
	but rev-parse --show-superproject-working-tree >out &&
	test_must_be_empty out &&

	test_create_repo super &&
	test_cummit -C super test_cummit &&
	test_create_repo sub &&
	test_cummit -C sub test_cummit &&
	but -C super submodule add ../sub dir/sub &&
	echo $(pwd)/super >expect  &&
	but -C super/dir/sub rev-parse --show-superproject-working-tree >out &&
	test_cmp expect out &&

	test_cummit -C super submodule_add &&
	but -C super checkout -b branch1 &&
	but -C super/dir/sub checkout -b branch1 &&
	test_cummit -C super/dir/sub branch1_cummit &&
	but -C super add dir/sub &&
	test_cummit -C super branch1_cummit &&
	but -C super checkout -b branch2 main &&
	but -C super/dir/sub checkout -b branch2 main &&
	test_cummit -C super/dir/sub branch2_cummit &&
	but -C super add dir/sub &&
	test_cummit -C super branch2_cummit &&
	test_must_fail but -C super merge branch1 &&

	but -C super/dir/sub rev-parse --show-superproject-working-tree >out &&
	test_cmp expect out
'

# at least one external project depends on this behavior:
test_expect_success 'rev-parse --since= unsqueezed ordering' '
	x1=--since=1970-01-01T00:00:01Z &&
	x2=--since=1970-01-01T00:00:02Z &&
	x3=--since=1970-01-01T00:00:03Z &&
	but rev-parse $x1 $x1 $x3 $x2 >actual &&
	cat >expect <<-EOF &&
	--max-age=1
	--max-age=1
	--max-age=3
	--max-age=2
	EOF
	test_cmp expect actual
'

test_done
