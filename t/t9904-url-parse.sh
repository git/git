#!/bin/sh
#
# Copyright © 2024 Matheus Afonso Martins Moreira
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
	git url-parse "file:///" &&
	git url-parse "file://"
'

test_expect_success 'git url-parse -c protocol -- ssh syntax' '
	test ssh = "$(git url-parse -c protocol "ssh://user@example.com:1234/repository/path")" &&
	test ssh = "$(git url-parse -c protocol "ssh://user@example.com/repository/path")" &&
	test ssh = "$(git url-parse -c protocol "ssh://example.com:1234/repository/path")" &&
	test ssh = "$(git url-parse -c protocol "ssh://example.com/repository/path")"
'

test_expect_success 'git url-parse -c protocol -- git syntax' '
	test git = "$(git url-parse -c protocol "git://example.com:1234/repository/path")" &&
	test git = "$(git url-parse -c protocol "git://example.com/repository/path")"
'

test_expect_success 'git url-parse -c protocol -- http syntax' '
	test https = "$(git url-parse -c protocol "https://example.com:1234/repository/path")" &&
	test https = "$(git url-parse -c protocol "https://example.com/repository/path")" &&
	test http = "$(git url-parse -c protocol "http://example.com:1234/repository/path")" &&
	test http = "$(git url-parse -c protocol "http://example.com/repository/path")"
'

test_expect_success 'git url-parse -c protocol -- scp syntax' '
	test ssh = "$(git url-parse -c protocol "user@example.com:/repository/path")" &&
	test ssh = "$(git url-parse -c protocol "example.com:/repository/path")"
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

test_done
