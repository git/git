#!/bin/sh

test_description='urlmatch URL normalization'
. ./test-lib.sh

# The base name of the test url files
tu="$TEST_DIRECTORY/t0110/url"

# Note that only file: URLs should be allowed without a host

test_expect_success 'url scheme' '
	! test-tool urlmatch-normalization "" &&
	! test-tool urlmatch-normalization "_" &&
	! test-tool urlmatch-normalization "scheme" &&
	! test-tool urlmatch-normalization "scheme:" &&
	! test-tool urlmatch-normalization "scheme:/" &&
	! test-tool urlmatch-normalization "scheme://" &&
	! test-tool urlmatch-normalization "file" &&
	! test-tool urlmatch-normalization "file:" &&
	! test-tool urlmatch-normalization "file:/" &&
	test-tool urlmatch-normalization "file://" &&
	! test-tool urlmatch-normalization "://acme.co" &&
	! test-tool urlmatch-normalization "x_test://acme.co" &&
	! test-tool urlmatch-normalization "-test://acme.co" &&
	! test-tool urlmatch-normalization "0test://acme.co" &&
	! test-tool urlmatch-normalization "+test://acme.co" &&
	! test-tool urlmatch-normalization ".test://acme.co" &&
	! test-tool urlmatch-normalization "schem%6e://" &&
	test-tool urlmatch-normalization "x-Test+v1.0://acme.co" &&
	test "$(test-tool urlmatch-normalization -p "AbCdeF://x.Y")" = "abcdef://x.y/"
'

test_expect_success 'url authority' '
	! test-tool urlmatch-normalization "scheme://user:pass@" &&
	! test-tool urlmatch-normalization "scheme://?" &&
	! test-tool urlmatch-normalization "scheme://#" &&
	! test-tool urlmatch-normalization "scheme:///" &&
	! test-tool urlmatch-normalization "scheme://:" &&
	! test-tool urlmatch-normalization "scheme://:555" &&
	test-tool urlmatch-normalization "file://user:pass@" &&
	test-tool urlmatch-normalization "file://?" &&
	test-tool urlmatch-normalization "file://#" &&
	test-tool urlmatch-normalization "file:///" &&
	test-tool urlmatch-normalization "file://:" &&
	! test-tool urlmatch-normalization "file://:555" &&
	test-tool urlmatch-normalization "scheme://user:pass@host" &&
	test-tool urlmatch-normalization "scheme://@host" &&
	test-tool urlmatch-normalization "scheme://%00@host" &&
	! test-tool urlmatch-normalization "scheme://%%@host" &&
	test-tool urlmatch-normalization "scheme://host_" &&
	test-tool urlmatch-normalization "scheme://user:pass@host/" &&
	test-tool urlmatch-normalization "scheme://@host/" &&
	test-tool urlmatch-normalization "scheme://host/" &&
	test-tool urlmatch-normalization "scheme://host?x" &&
	test-tool urlmatch-normalization "scheme://host#x" &&
	test-tool urlmatch-normalization "scheme://host/@" &&
	test-tool urlmatch-normalization "scheme://host?@x" &&
	test-tool urlmatch-normalization "scheme://host#@x" &&
	test-tool urlmatch-normalization "scheme://[::1]" &&
	test-tool urlmatch-normalization "scheme://[::1]/" &&
	! test-tool urlmatch-normalization "scheme://hos%41/" &&
	test-tool urlmatch-normalization "scheme://[invalid....:/" &&
	test-tool urlmatch-normalization "scheme://invalid....:]/" &&
	! test-tool urlmatch-normalization "scheme://invalid....:[/" &&
	! test-tool urlmatch-normalization "scheme://invalid....:["
'

test_expect_success 'url port checks' '
	test-tool urlmatch-normalization "xyz://q@some.host:" &&
	test-tool urlmatch-normalization "xyz://q@some.host:456/" &&
	! test-tool urlmatch-normalization "xyz://q@some.host:0" &&
	! test-tool urlmatch-normalization "xyz://q@some.host:0000000" &&
	test-tool urlmatch-normalization "xyz://q@some.host:0000001?" &&
	test-tool urlmatch-normalization "xyz://q@some.host:065535#" &&
	test-tool urlmatch-normalization "xyz://q@some.host:65535" &&
	! test-tool urlmatch-normalization "xyz://q@some.host:65536" &&
	! test-tool urlmatch-normalization "xyz://q@some.host:99999" &&
	! test-tool urlmatch-normalization "xyz://q@some.host:100000" &&
	! test-tool urlmatch-normalization "xyz://q@some.host:100001" &&
	test-tool urlmatch-normalization "http://q@some.host:80" &&
	test-tool urlmatch-normalization "https://q@some.host:443" &&
	test-tool urlmatch-normalization "http://q@some.host:80/" &&
	test-tool urlmatch-normalization "https://q@some.host:443?" &&
	! test-tool urlmatch-normalization "http://q@:8008" &&
	! test-tool urlmatch-normalization "http://:8080" &&
	! test-tool urlmatch-normalization "http://:" &&
	test-tool urlmatch-normalization "xyz://q@some.host:456/" &&
	test-tool urlmatch-normalization "xyz://[::1]:456/" &&
	test-tool urlmatch-normalization "xyz://[::1]:/" &&
	! test-tool urlmatch-normalization "xyz://[::1]:000/" &&
	! test-tool urlmatch-normalization "xyz://[::1]:0%300/" &&
	! test-tool urlmatch-normalization "xyz://[::1]:0x80/" &&
	! test-tool urlmatch-normalization "xyz://[::1]:4294967297/" &&
	! test-tool urlmatch-normalization "xyz://[::1]:030f/"
'

test_expect_success 'url port normalization' '
	test "$(test-tool urlmatch-normalization -p "http://x:800")" = "http://x:800/" &&
	test "$(test-tool urlmatch-normalization -p "http://x:0800")" = "http://x:800/" &&
	test "$(test-tool urlmatch-normalization -p "http://x:00000800")" = "http://x:800/" &&
	test "$(test-tool urlmatch-normalization -p "http://x:065535")" = "http://x:65535/" &&
	test "$(test-tool urlmatch-normalization -p "http://x:1")" = "http://x:1/" &&
	test "$(test-tool urlmatch-normalization -p "http://x:80")" = "http://x/" &&
	test "$(test-tool urlmatch-normalization -p "http://x:080")" = "http://x/" &&
	test "$(test-tool urlmatch-normalization -p "http://x:000000080")" = "http://x/" &&
	test "$(test-tool urlmatch-normalization -p "https://x:443")" = "https://x/" &&
	test "$(test-tool urlmatch-normalization -p "https://x:0443")" = "https://x/" &&
	test "$(test-tool urlmatch-normalization -p "https://x:000000443")" = "https://x/"
'

test_expect_success 'url general escapes' '
	! test-tool urlmatch-normalization "http://x.y?%fg" &&
	test "$(test-tool urlmatch-normalization -p "X://W/%7e%41^%3a")" = "x://w/~A%5E%3A" &&
	test "$(test-tool urlmatch-normalization -p "X://W/:/?#[]@")" = "x://w/:/?#[]@" &&
	test "$(test-tool urlmatch-normalization -p "X://W/$&()*+,;=")" = "x://w/$&()*+,;=" &&
	test "$(test-tool urlmatch-normalization -p "X://W/'\''")" = "x://w/'\''" &&
	test "$(test-tool urlmatch-normalization -p "X://W?'\!'")" = "x://w/?'\!'"
'

test_expect_success !MINGW 'url high-bit escapes' '
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-1")")" = "x://q/%01%02%03%04%05%06%07%08%0E%0F%10%11%12" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-2")")" = "x://q/%13%14%15%16%17%18%19%1B%1C%1D%1E%1F%7F" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-3")")" = "x://q/%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-4")")" = "x://q/%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-5")")" = "x://q/%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-6")")" = "x://q/%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-7")")" = "x://q/%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-8")")" = "x://q/%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-9")")" = "x://q/%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF" &&
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-10")")" = "x://q/%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF"
'

test_expect_success 'url utf-8 escapes' '
	test "$(test-tool urlmatch-normalization -p "$(cat "$tu-11")")" = "x://q/%C2%80%DF%BF%E0%A0%80%EF%BF%BD%F0%90%80%80%F0%AF%BF%BD"
'

test_expect_success 'url username/password escapes' '
	test "$(test-tool urlmatch-normalization -p "x://%41%62(^):%70+d@foo")" = "x://Ab(%5E):p+d@foo/"
'

test_expect_success 'url normalized lengths' '
	test "$(test-tool urlmatch-normalization -l "Http://%4d%65:%4d^%70@The.Host")" = 25 &&
	test "$(test-tool urlmatch-normalization -l "http://%41:%42@x.y/%61/")" = 17 &&
	test "$(test-tool urlmatch-normalization -l "http://@x.y/^")" = 15
'

test_expect_success 'url . and .. segments' '
	test "$(test-tool urlmatch-normalization -p "x://y/.")" = "x://y/" &&
	test "$(test-tool urlmatch-normalization -p "x://y/./")" = "x://y/" &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/.")" = "x://y/a" &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/./")" = "x://y/a/" &&
	test "$(test-tool urlmatch-normalization -p "x://y/.?")" = "x://y/?" &&
	test "$(test-tool urlmatch-normalization -p "x://y/./?")" = "x://y/?" &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/.?")" = "x://y/a?" &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/./?")" = "x://y/a/?" &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/./b/.././../c")" = "x://y/c" &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/./b/../.././c/")" = "x://y/c/" &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/./b/.././../c/././.././.")" = "x://y/" &&
	! test-tool urlmatch-normalization "x://y/a/./b/.././../c/././.././.." &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/./?/././..")" = "x://y/a/?/././.." &&
	test "$(test-tool urlmatch-normalization -p "x://y/%2e/")" = "x://y/" &&
	test "$(test-tool urlmatch-normalization -p "x://y/%2E/")" = "x://y/" &&
	test "$(test-tool urlmatch-normalization -p "x://y/a/%2e./")" = "x://y/" &&
	test "$(test-tool urlmatch-normalization -p "x://y/b/.%2E/")" = "x://y/" &&
	test "$(test-tool urlmatch-normalization -p "x://y/c/%2e%2E/")" = "x://y/"
'

# http://@foo specifies an empty user name but does not specify a password
# http://foo  specifies neither a user name nor a password
# So they should not be equivalent
test_expect_success 'url equivalents' '
	test-tool urlmatch-normalization "httP://x" "Http://X/" &&
	test-tool urlmatch-normalization "Http://%4d%65:%4d^%70@The.Host" "hTTP://Me:%4D^p@the.HOST:80/" &&
	! test-tool urlmatch-normalization "https://@x.y/^" "httpS://x.y:443/^" &&
	test-tool urlmatch-normalization "https://@x.y/^" "httpS://@x.y:0443/^" &&
	test-tool urlmatch-normalization "https://@x.y/^/../abc" "httpS://@x.y:0443/abc" &&
	test-tool urlmatch-normalization "https://@x.y/^/.." "httpS://@x.y:0443/"
'

test_done
