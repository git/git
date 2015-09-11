#!/bin/sh

test_description='test for-each-refs usage of ref-filter APIs'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

if ! test_have_prereq GPG
then
	skip_all="skipping for-each-ref tests, GPG not available"
	test_done
fi

test_expect_success 'setup some history and refs' '
	test_commit one &&
	test_commit two &&
	test_commit three &&
	git checkout -b side &&
	test_commit four &&
	git tag -s -m "A signed tag message" signed-tag &&
	git tag -s -m "Annonated doubly" double-tag signed-tag &&
	git checkout master &&
	git update-ref refs/odd/spot master
'

test_expect_success 'filtering with --points-at' '
	cat >expect <<-\EOF &&
	refs/heads/master
	refs/odd/spot
	refs/tags/three
	EOF
	git for-each-ref --format="%(refname)" --points-at=master >actual &&
	test_cmp expect actual
'

test_expect_success 'check signed tags with --points-at' '
	sed -e "s/Z$//" >expect <<-\EOF &&
	refs/heads/side Z
	refs/tags/four Z
	refs/tags/signed-tag four
	EOF
	git for-each-ref --format="%(refname) %(*subject)" --points-at=side >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --merged' '
	cat >expect <<-\EOF &&
	refs/heads/master
	refs/odd/spot
	refs/tags/one
	refs/tags/three
	refs/tags/two
	EOF
	git for-each-ref --format="%(refname)" --merged=master >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --no-merged' '
	cat >expect <<-\EOF &&
	refs/heads/side
	refs/tags/double-tag
	refs/tags/four
	refs/tags/signed-tag
	EOF
	git for-each-ref --format="%(refname)" --no-merged=master >actual &&
	test_cmp expect actual
'

test_expect_success 'filtering with --contains' '
	cat >expect <<-\EOF &&
	refs/heads/master
	refs/heads/side
	refs/odd/spot
	refs/tags/double-tag
	refs/tags/four
	refs/tags/signed-tag
	refs/tags/three
	refs/tags/two
	EOF
	git for-each-ref --format="%(refname)" --contains=two >actual &&
	test_cmp expect actual
'

test_expect_success '%(color) must fail' '
	test_must_fail git for-each-ref --format="%(color)%(refname)"
'

test_expect_success 'left alignment is default' '
	cat >expect <<-\EOF &&
	refname is refs/heads/master  |refs/heads/master
	refname is refs/heads/side    |refs/heads/side
	refname is refs/odd/spot      |refs/odd/spot
	refname is refs/tags/double-tag|refs/tags/double-tag
	refname is refs/tags/four     |refs/tags/four
	refname is refs/tags/one      |refs/tags/one
	refname is refs/tags/signed-tag|refs/tags/signed-tag
	refname is refs/tags/three    |refs/tags/three
	refname is refs/tags/two      |refs/tags/two
	EOF
	git for-each-ref --format="%(align:30)refname is %(refname)%(end)|%(refname)" >actual &&
	test_cmp expect actual
'

test_expect_success 'middle alignment' '
	cat >expect <<-\EOF &&
	| refname is refs/heads/master |refs/heads/master
	|  refname is refs/heads/side  |refs/heads/side
	|   refname is refs/odd/spot   |refs/odd/spot
	|refname is refs/tags/double-tag|refs/tags/double-tag
	|  refname is refs/tags/four   |refs/tags/four
	|   refname is refs/tags/one   |refs/tags/one
	|refname is refs/tags/signed-tag|refs/tags/signed-tag
	|  refname is refs/tags/three  |refs/tags/three
	|   refname is refs/tags/two   |refs/tags/two
	EOF
	git for-each-ref --format="|%(align:middle,30)refname is %(refname)%(end)|%(refname)" >actual &&
	test_cmp expect actual
'

test_expect_success 'right alignment' '
	cat >expect <<-\EOF &&
	|  refname is refs/heads/master|refs/heads/master
	|    refname is refs/heads/side|refs/heads/side
	|      refname is refs/odd/spot|refs/odd/spot
	|refname is refs/tags/double-tag|refs/tags/double-tag
	|     refname is refs/tags/four|refs/tags/four
	|      refname is refs/tags/one|refs/tags/one
	|refname is refs/tags/signed-tag|refs/tags/signed-tag
	|    refname is refs/tags/three|refs/tags/three
	|      refname is refs/tags/two|refs/tags/two
	EOF
	git for-each-ref --format="|%(align:30,right)refname is %(refname)%(end)|%(refname)" >actual &&
	test_cmp expect actual
'

# Individual atoms inside %(align:...) and %(end) must not be quoted.

test_expect_success 'alignment with format quote' "
	cat >expect <<-\EOF &&
	|'      '\''master| A U Thor'\''      '|
	|'       '\''side| A U Thor'\''       '|
	|'     '\''odd/spot| A U Thor'\''     '|
	|'        '\''double-tag| '\''        '|
	|'       '\''four| A U Thor'\''       '|
	|'       '\''one| A U Thor'\''        '|
	|'        '\''signed-tag| '\''        '|
	|'      '\''three| A U Thor'\''       '|
	|'       '\''two| A U Thor'\''        '|
	EOF
	git for-each-ref --shell --format=\"|%(align:30,middle)'%(refname:short)| %(authorname)'%(end)|\" >actual &&
	test_cmp expect actual
"

test_expect_success 'nested alignment with quote formatting' "
	cat >expect <<-\EOF &&
	|'         master               '|
	|'           side               '|
	|'       odd/spot               '|
	|'     double-tag               '|
	|'           four               '|
	|'            one               '|
	|'     signed-tag               '|
	|'          three               '|
	|'            two               '|
	EOF
	git for-each-ref --shell --format='|%(align:30,left)%(align:15,right)%(refname:short)%(end)%(end)|' >actual &&
	test_cmp expect actual
"

test_expect_success 'check `%(contents:lines=1)`' '
	cat >expect <<-\EOF &&
	master |three
	side |four
	odd/spot |three
	double-tag |Annonated doubly
	four |four
	one |one
	signed-tag |A signed tag message
	three |three
	two |two
	EOF
	git for-each-ref --format="%(refname:short) |%(contents:lines=1)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check `%(contents:lines=0)`' '
	cat >expect <<-\EOF &&
	master |
	side |
	odd/spot |
	double-tag |
	four |
	one |
	signed-tag |
	three |
	two |
	EOF
	git for-each-ref --format="%(refname:short) |%(contents:lines=0)" >actual &&
	test_cmp expect actual
'

test_expect_success 'check `%(contents:lines=99999)`' '
	cat >expect <<-\EOF &&
	master |three
	side |four
	odd/spot |three
	double-tag |Annonated doubly
	four |four
	one |one
	signed-tag |A signed tag message
	three |three
	two |two
	EOF
	git for-each-ref --format="%(refname:short) |%(contents:lines=99999)" >actual &&
	test_cmp expect actual
'

test_expect_success '`%(contents:lines=-1)` should fail' '
	test_must_fail git for-each-ref --format="%(refname:short) |%(contents:lines=-1)"
'

test_done
