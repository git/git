#!/bin/sh

test_description='git grep --open-files-in-pager
'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pager.sh
unset PAGER GIT_PAGER

test_expect_success 'setup' '
	test_commit initial grep.h "
enum grep_pat_token {
	GREP_PATTERN,
	GREP_PATTERN_HEAD,
	GREP_PATTERN_BODY,
	GREP_AND,
	GREP_OPEN_PAREN,
	GREP_CLOSE_PAREN,
	GREP_NOT,
	GREP_OR,
};" &&

	test_commit add-user revision.c "
	}
	if (seen_dashdash)
		read_pathspec_from_stdin(revs, &sb, prune);
	strbuf_release(&sb);
}

static void add_grep(struct rev_info *revs, const char *ptn, enum grep_pat_token what)
{
	append_grep_pattern(&revs->grep_filter, ptn, \"command line\", 0, what);
" &&

	mkdir subdir &&
	test_commit subdir subdir/grep.c "enum grep_pat_token" &&

	test_commit uninteresting unrelated "hello, world" &&

	echo GREP_PATTERN >untracked
'

test_expect_success SIMPLEPAGER 'git grep -O' '
	cat >$less <<-\EOF &&
	#!/bin/sh
	printf "%s\n" "$@" >pager-args
	EOF
	chmod +x $less &&
	cat >expect.less <<-\EOF &&
	+/*GREP_PATTERN
	grep.h
	EOF
	echo grep.h >expect.notless &&
	>empty &&

	PATH=.:$PATH git grep -O GREP_PATTERN >out &&
	{
		test_cmp expect.less pager-args ||
		test_cmp expect.notless pager-args
	} &&
	test_cmp empty out
'

test_expect_success 'git grep -O --cached' '
	test_must_fail git grep --cached -O GREP_PATTERN >out 2>msg &&
	test_i18ngrep open-files-in-pager msg
'

test_expect_success 'git grep -O --no-index' '
	rm -f expect.less pager-args out &&
	cat >expect <<-\EOF &&
	grep.h
	untracked
	EOF
	>empty &&

	(
		GIT_PAGER='\''printf "%s\n" >pager-args'\'' &&
		export GIT_PAGER &&
		git grep --no-index -O GREP_PATTERN >out
	) &&
	test_cmp expect pager-args &&
	test_cmp empty out
'

test_expect_success 'setup: fake "less"' '
	cat >less <<-\EOF &&
	#!/bin/sh
	printf "%s\n" "$@" >actual
	EOF
	chmod +x less
'

test_expect_success 'git grep -O jumps to line in less' '
	cat >expect <<-\EOF &&
	+/*GREP_PATTERN
	grep.h
	EOF
	>empty &&

	GIT_PAGER=./less git grep -O GREP_PATTERN >out &&
	test_cmp expect actual &&
	test_cmp empty out &&

	git grep -O./less GREP_PATTERN >out2 &&
	test_cmp expect actual &&
	test_cmp empty out2
'

test_expect_success 'modified file' '
	rm -f actual &&
	cat >expect <<-\EOF &&
	+/*enum grep_pat_token
	grep.h
	revision.c
	subdir/grep.c
	unrelated
	EOF
	>empty &&

	echo "enum grep_pat_token" >unrelated &&
	test_when_finished "git checkout HEAD unrelated" &&
	GIT_PAGER=./less git grep -F -O "enum grep_pat_token" >out &&
	test_cmp expect actual &&
	test_cmp empty out
'

test_config() {
	git config "$1" "$2" &&
	test_when_finished "git config --unset $1"
}

test_expect_success 'copes with color settings' '
	rm -f actual &&
	echo grep.h >expect &&
	test_config color.grep always &&
	test_config color.grep.filename yellow &&
	test_config color.grep.separator green &&
	git grep -O'\''printf "%s\n" >actual'\'' GREP_AND &&
	test_cmp expect actual
'

test_expect_success 'run from subdir' '
	rm -f actual &&
	echo grep.c >expect &&
	>empty &&

	(
		cd subdir &&
		export GIT_PAGER &&
		GIT_PAGER='\''printf "%s\n" >../args'\'' &&
		git grep -O "enum grep_pat_token" >../out &&
		git grep -O"pwd >../dir; :" "enum grep_pat_token" >../out2
	) &&
	case $(cat dir) in
	*subdir)
		: good
		;;
	*)
		false
		;;
	esac &&
	test_cmp expect args &&
	test_cmp empty out &&
	test_cmp empty out2
'

test_done
