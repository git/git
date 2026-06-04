#!/bin/sh
#
# Copyright (c) 2024 Matheus Afonso Martins Moreira
#

test_description='git url-parse tests'

. ./test-lib.sh

test_expect_success 'git url-parse -- ssh syntax' '
	git url-parse "ssh://user@example.com:1234/repository/path" &&
	git url-parse "ssh://user@example.com/repository/path" &&
	git url-parse "ssh://example.com:1234/repository/path" &&
	git url-parse "ssh://example.com/repository/path"
'

test_expect_success 'git url-parse -- git syntax' '
	git url-parse "git://example.com:1234/repository/path" &&
	git url-parse "git://example.com/repository/path"
'

test_expect_success 'git url-parse -- http syntax' '
	git url-parse "https://example.com:1234/repository/path" &&
	git url-parse "https://example.com/repository/path" &&
	git url-parse "http://example.com:1234/repository/path" &&
	git url-parse "http://example.com/repository/path"
'

test_expect_success 'git url-parse -- scp syntax' '
	git url-parse "user@example.com:/repository/path" &&
	git url-parse "example.com:/repository/path"
'

test_expect_success 'git url-parse -- username expansion - ssh syntax' '
	git url-parse "ssh://user@example.com:1234/~user/repository" &&
	git url-parse "ssh://user@example.com/~user/repository" &&
	git url-parse "ssh://example.com:1234/~user/repository" &&
	git url-parse "ssh://example.com/~user/repository"
'

test_expect_success 'git url-parse -- username expansion - git syntax' '
	git url-parse "git://example.com:1234/~user/repository" &&
	git url-parse "git://example.com/~user/repository"
'

test_expect_success 'git url-parse -- username expansion - scp syntax' '
	git url-parse "user@example.com:~user/repository" &&
	git url-parse "example.com:~user/repository"
'

test_expect_success 'git url-parse -- file urls' '
	git url-parse "file:///repository/path" &&
	git url-parse "file://"
'

test_expect_success 'git url-parse -c scheme -- ssh syntax' '
	test ssh = "$(git url-parse -c scheme "ssh://user@example.com:1234/repository/path")" &&
	test ssh = "$(git url-parse -c scheme "ssh://user@example.com/repository/path")" &&
	test ssh = "$(git url-parse -c scheme "ssh://example.com:1234/repository/path")" &&
	test ssh = "$(git url-parse -c scheme "ssh://example.com/repository/path")"
'

test_expect_success 'git url-parse -c scheme -- git syntax' '
	test git = "$(git url-parse -c scheme "git://example.com:1234/repository/path")" &&
	test git = "$(git url-parse -c scheme "git://example.com/repository/path")"
'

test_expect_success 'git url-parse -c scheme -- http syntax' '
	test https = "$(git url-parse -c scheme "https://example.com:1234/repository/path")" &&
	test https = "$(git url-parse -c scheme "https://example.com/repository/path")" &&
	test http = "$(git url-parse -c scheme "http://example.com:1234/repository/path")" &&
	test http = "$(git url-parse -c scheme "http://example.com/repository/path")"
'

test_expect_success 'git url-parse -c scheme -- scp syntax' '
	test ssh = "$(git url-parse -c scheme "user@example.com:/repository/path")" &&
	test ssh = "$(git url-parse -c scheme "example.com:/repository/path")"
'

test_expect_success 'git url-parse -c user -- ssh syntax' '
	test user = "$(git url-parse -c user "ssh://user@example.com:1234/repository/path")" &&
	test user = "$(git url-parse -c user "ssh://user@example.com/repository/path")" &&
	test "" = "$(git url-parse -c user "ssh://example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c user "ssh://example.com/repository/path")"
'

test_expect_success 'git url-parse -c user -- git syntax' '
	test "" = "$(git url-parse -c user "git://example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c user "git://example.com/repository/path")"
'

test_expect_success 'git url-parse -c user -- http syntax' '
	test "" = "$(git url-parse -c user "https://example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c user "https://example.com/repository/path")" &&
	test "" = "$(git url-parse -c user "http://example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c user "http://example.com/repository/path")"
'

test_expect_success 'git url-parse -c user -- scp syntax' '
	test user = "$(git url-parse -c user "user@example.com:/repository/path")" &&
	test "" = "$(git url-parse -c user "example.com:/repository/path")"
'

test_expect_success 'git url-parse -c password -- http syntax' '
	test secret = "$(git url-parse -c password "https://user:secret@example.com:1234/repository/path")" &&
	test secret = "$(git url-parse -c password "http://user:secret@example.com/repository/path")" &&
	test "" = "$(git url-parse -c password "https://user@example.com/repository/path")" &&
	test "" = "$(git url-parse -c password "https://example.com/repository/path")"
'

test_expect_success 'git url-parse -c host -- ssh syntax' '
	test example.com = "$(git url-parse -c host "ssh://user@example.com:1234/repository/path")" &&
	test example.com = "$(git url-parse -c host "ssh://user@example.com/repository/path")" &&
	test example.com = "$(git url-parse -c host "ssh://example.com:1234/repository/path")" &&
	test example.com = "$(git url-parse -c host "ssh://example.com/repository/path")"
'

test_expect_success 'git url-parse -c host -- git syntax' '
	test example.com = "$(git url-parse -c host "git://example.com:1234/repository/path")" &&
	test example.com = "$(git url-parse -c host "git://example.com/repository/path")"
'

test_expect_success 'git url-parse -c host -- http syntax' '
	test example.com = "$(git url-parse -c host "https://example.com:1234/repository/path")" &&
	test example.com = "$(git url-parse -c host "https://example.com/repository/path")" &&
	test example.com = "$(git url-parse -c host "http://example.com:1234/repository/path")" &&
	test example.com = "$(git url-parse -c host "http://example.com/repository/path")"
'

test_expect_success 'git url-parse -c host -- scp syntax' '
	test example.com = "$(git url-parse -c host "user@example.com:/repository/path")" &&
	test example.com = "$(git url-parse -c host "example.com:/repository/path")"
'

test_expect_success 'git url-parse -c port -- ssh syntax' '
	test 1234 = "$(git url-parse -c port "ssh://user@example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c port "ssh://user@example.com/repository/path")" &&
	test 1234 = "$(git url-parse -c port "ssh://example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c port "ssh://example.com/repository/path")"
'

test_expect_success 'git url-parse -c port -- git syntax' '
	test 1234 = "$(git url-parse -c port "git://example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c port "git://example.com/repository/path")"
'

test_expect_success 'git url-parse -c port -- http syntax' '
	test 1234 = "$(git url-parse -c port "https://example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c port "https://example.com/repository/path")" &&
	test 1234 = "$(git url-parse -c port "http://example.com:1234/repository/path")" &&
	test "" = "$(git url-parse -c port "http://example.com/repository/path")"
'

test_expect_success 'git url-parse -c port -- scp syntax' '
	test "" = "$(git url-parse -c port "user@example.com:/repository/path")" &&
	test "" = "$(git url-parse -c port "example.com:/repository/path")"
'

test_expect_success 'git url-parse -c path -- ssh syntax' '
	test "/repository/path" = "$(git url-parse -c path "ssh://user@example.com:1234/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "ssh://user@example.com/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "ssh://example.com:1234/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "ssh://example.com/repository/path")"
'

test_expect_success 'git url-parse -c path -- git syntax' '
	test "/repository/path" = "$(git url-parse -c path "git://example.com:1234/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "git://example.com/repository/path")"
'

test_expect_success 'git url-parse -c path -- http syntax' '
	test "/repository/path" = "$(git url-parse -c path "https://example.com:1234/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "https://example.com/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "http://example.com:1234/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "http://example.com/repository/path")"
'

test_expect_success 'git url-parse -c path -- scp syntax' '
	test "/repository/path" = "$(git url-parse -c path "user@example.com:/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "example.com:/repository/path")"
'

test_expect_success 'git url-parse -c path -- username expansion - ssh syntax' '
	test "~user/repository" = "$(git url-parse -c path "ssh://user@example.com:1234/~user/repository")" &&
	test "~user/repository" = "$(git url-parse -c path "ssh://user@example.com/~user/repository")" &&
	test "~user/repository" = "$(git url-parse -c path "ssh://example.com:1234/~user/repository")" &&
	test "~user/repository" = "$(git url-parse -c path "ssh://example.com/~user/repository")"
'

test_expect_success 'git url-parse -c path -- username expansion - git syntax' '
	test "~user/repository" = "$(git url-parse -c path "git://example.com:1234/~user/repository")" &&
	test "~user/repository" = "$(git url-parse -c path "git://example.com/~user/repository")"
'

test_expect_success 'git url-parse -c path -- username expansion - scp syntax' '
	test "~user/repository" = "$(git url-parse -c path "user@example.com:~user/repository")" &&
	test "~user/repository" = "$(git url-parse -c path "example.com:~user/repository")"
'

test_expect_success 'git url-parse -c path -- username expansion strips query and fragment' '
	test "~user/repository" = "$(git url-parse -c path "ssh://example.com/~user/repository?query")" &&
	test "~user/repository" = "$(git url-parse -c path "ssh://example.com/~user/repository#fragment")" &&
	test "~user/repository" = "$(git url-parse -c path "git://example.com/~user/repository?query")" &&
	test "~user/repository" = "$(git url-parse -c path "user@example.com:~user/repository?query")"
'

test_expect_success 'git url-parse -- ssh syntax with IPv6' '
	git url-parse "ssh://user@[::1]:1234/repository/path" &&
	git url-parse "ssh://user@[::1]/repository/path" &&
	git url-parse "ssh://[::1]:1234/repository/path" &&
	git url-parse "ssh://[::1]/repository/path" &&
	git url-parse "ssh://[2001:db8::1]/repository/path"
'

test_expect_success 'git url-parse -- git syntax with IPv6' '
	git url-parse "git://[::1]:9418/repository/path" &&
	git url-parse "git://[::1]/repository/path"
'

test_expect_success 'git url-parse -- http syntax with IPv6' '
	git url-parse "https://[::1]:1234/repository/path" &&
	git url-parse "https://[::1]/repository/path" &&
	git url-parse "http://[2001:db8::1]/repository/path"
'

test_expect_success 'git url-parse -c host -- IPv6 in URL form' '
	test "[::1]" = "$(git url-parse -c host "ssh://user@[::1]:1234/repository/path")" &&
	test "[::1]" = "$(git url-parse -c host "ssh://[::1]/repository/path")" &&
	test "[2001:db8::1]" = "$(git url-parse -c host "ssh://[2001:db8::1]/repository/path")" &&
	test "[::1]" = "$(git url-parse -c host "git://[::1]/repository/path")" &&
	test "[2001:db8::1]" = "$(git url-parse -c host "https://[2001:db8::1]/repository/path")"
'

test_expect_success 'git url-parse -c port -- IPv6 in URL form' '
	test 1234 = "$(git url-parse -c port "ssh://user@[::1]:1234/repository/path")" &&
	test "" = "$(git url-parse -c port "ssh://[::1]/repository/path")" &&
	test 9418 = "$(git url-parse -c port "git://[::1]:9418/repository/path")"
'

test_expect_success 'git url-parse -- scp syntax with IPv6' '
	git url-parse "[::1]:repository/path" &&
	git url-parse "user@[::1]:repository/path" &&
	git url-parse "[2001:db8::1]:repo"
'

test_expect_success 'git url-parse -- scp syntax with bracketed hostname' '
	git url-parse "[myhost]:src" &&
	git url-parse "user@[myhost]:src"
'

test_expect_success 'git url-parse -- scp syntax with bracketed host:port' '
	git url-parse "[myhost:123]:src" &&
	git url-parse "user@[myhost:123]:src"
'

test_expect_success 'git url-parse -c host -- scp+IPv6' '
	test "[::1]" = "$(git url-parse -c host "[::1]:repository/path")" &&
	test "[::1]" = "$(git url-parse -c host "user@[::1]:repository/path")" &&
	test "[2001:db8::1]" = "$(git url-parse -c host "[2001:db8::1]:repo")"
'

test_expect_success 'git url-parse -c path -- scp+IPv6' '
	test "/repository/path" = "$(git url-parse -c path "[::1]:/repository/path")" &&
	test "/repository/path" = "$(git url-parse -c path "[::1]:repository/path")" &&
	test "/repo" = "$(git url-parse -c path "[2001:db8::1]:repo")"
'

test_expect_success 'git url-parse -c host,port,path -- scp [host:port]:src' '
	test myhost = "$(git url-parse -c host "[myhost:123]:src")" &&
	test 123 = "$(git url-parse -c port "[myhost:123]:src")" &&
	test "/src" = "$(git url-parse -c path "[myhost:123]:src")"
'

test_expect_success 'git url-parse -c host,path -- scp [host]:src' '
	test myhost = "$(git url-parse -c host "[myhost]:src")" &&
	test "/src" = "$(git url-parse -c path "[myhost]:src")"
'

test_expect_success 'git url-parse -c user -- scp with user@ and brackets' '
	test user = "$(git url-parse -c user "user@[::1]:repo")" &&
	test user = "$(git url-parse -c user "user@[myhost:123]:src")" &&
	test user = "$(git url-parse -c user "user@[myhost]:src")"
'

test_expect_success 'git url-parse -- scp+IPv6 with username expansion' '
	test "~user/repo" = "$(git url-parse -c path "[::1]:~user/repo")" &&
	test "~user/repo" = "$(git url-parse -c path "user@[::1]:~user/repo")"
'

test_expect_success 'git url-parse fails on invalid URL' '
	test_must_fail git url-parse "not a url"
'

test_expect_success 'git url-parse helpful error for absolute local path' '
	test_must_fail git url-parse "/abs/path" 2>err &&
	test_grep "is not a URL" err &&
	test_grep "file:///" err
'

test_expect_success 'git url-parse helpful error for relative local path' '
	test_must_fail git url-parse "./rel" 2>err &&
	test_grep "is not a URL" err &&
	test_grep "absolute path" err
'

test_expect_success 'git url-parse fails on unknown -c component name' '
	test_must_fail git url-parse -c bogus "https://example.com/repo"
'

test_expect_success 'git url-parse fails on URL missing host' '
	test_must_fail git url-parse "https://"
'

test_expect_success 'git url-parse with no URL prints usage' '
	test_must_fail git url-parse 2>err &&
	test_grep "usage:" err
'

test_done
