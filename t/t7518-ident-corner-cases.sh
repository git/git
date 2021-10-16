#!/bin/sh

test_description='corner cases in ident strings'
. ./test-lib.sh

# confirm that we do not segfault _and_ that we do not say "(null)", as
# glibc systems will quietly handle our NULL pointer
#
# Note also that we can't use "env" here because we need to unset a variable,
# and "-u" is not portable.
test_expect_success 'empty name and missing email' '
	(
		sane_unset GIT_AUTHOR_EMAIL &&
		GIT_AUTHOR_NAME= &&
		test_must_fail git commit --allow-empty -m foo 2>err &&
		test_i18ngrep ! "(null)" err
	)
'

test_expect_success 'commit rejects all-crud name' '
	test_must_fail env GIT_AUTHOR_NAME=" .;<>" \
		git commit --allow-empty -m foo
'

# We must test the actual error message here, as an unwanted
# auto-detection could fail for other reasons.
test_expect_success 'empty configured name does not auto-detect' '
	(
		sane_unset GIT_AUTHOR_NAME &&
		test_must_fail \
			git -c user.name= commit --allow-empty -m foo 2>err &&
		test_i18ngrep "empty ident name" err &&
		test_i18ngrep "Author identity unknown" err
	)
'

test_expect_success 'empty configured name does not auto-detect for committer' '
	(
		sane_unset GIT_COMMITTER_NAME &&
		test_must_fail \
			git -c user.name= commit --allow-empty -m foo 2>err &&
		test_i18ngrep "empty ident name" err &&
		test_i18ngrep "Committer identity unknown" err
	)
'

test_done
