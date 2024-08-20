#include "test-lib.h"
#include "urlmatch.h"

static void check_url_normalizable(const char *url, unsigned int normalizable)
{
	char *url_norm = url_normalize(url, NULL);

	if (!check_int(normalizable, ==, url_norm ? 1 : 0))
		test_msg("input url: %s", url);
	free(url_norm);
}

static void check_normalized_url(const char *url, const char *expect)
{
	char *url_norm = url_normalize(url, NULL);

	if (!check_str(url_norm, expect))
		test_msg("input url: %s", url);
	free(url_norm);
}

static void compare_normalized_urls(const char *url1, const char *url2,
				    unsigned int equal)
{
	char *url1_norm = url_normalize(url1, NULL);
	char *url2_norm = url_normalize(url2, NULL);

	if (equal) {
		if (!check_str(url1_norm, url2_norm))
			test_msg("input url1: %s\n  input url2: %s", url1,
				 url2);
	} else if (!check_int(strcmp(url1_norm, url2_norm), !=, 0)) {
		test_msg(" normalized url1: %s\n   normalized url2: %s\n"
			 "  input url1: %s\n  input url2: %s",
			 url1_norm, url2_norm, url1, url2);
	}
	free(url1_norm);
	free(url2_norm);
}

static void check_normalized_url_length(const char *url, size_t len)
{
	struct url_info info;
	char *url_norm = url_normalize(url, &info);

	if (!check_int(info.url_len, ==, len))
		test_msg("     input url: %s\n  normalized url: %s", url,
			 url_norm);
	free(url_norm);
}

/* Note that only "file:" URLs should be allowed without a host */
static void t_url_scheme(void)
{
	check_url_normalizable("", 0);
	check_url_normalizable("_", 0);
	check_url_normalizable("scheme", 0);
	check_url_normalizable("scheme:", 0);
	check_url_normalizable("scheme:/", 0);
	check_url_normalizable("scheme://", 0);
	check_url_normalizable("file", 0);
	check_url_normalizable("file:", 0);
	check_url_normalizable("file:/", 0);
	check_url_normalizable("file://", 1);
	check_url_normalizable("://acme.co", 0);
	check_url_normalizable("x_test://acme.co", 0);
	check_url_normalizable("-test://acme.co", 0);
	check_url_normalizable("0test://acme.co", 0);
	check_url_normalizable("+test://acme.co", 0);
	check_url_normalizable(".test://acme.co", 0);
	check_url_normalizable("schem%6e://", 0);
	check_url_normalizable("x-Test+v1.0://acme.co", 1);
	check_normalized_url("AbCdeF://x.Y", "abcdef://x.y/");
}

static void t_url_authority(void)
{
	check_url_normalizable("scheme://user:pass@", 0);
	check_url_normalizable("scheme://?", 0);
	check_url_normalizable("scheme://#", 0);
	check_url_normalizable("scheme:///", 0);
	check_url_normalizable("scheme://:", 0);
	check_url_normalizable("scheme://:555", 0);
	check_url_normalizable("file://user:pass@", 1);
	check_url_normalizable("file://?", 1);
	check_url_normalizable("file://#", 1);
	check_url_normalizable("file:///", 1);
	check_url_normalizable("file://:", 1);
	check_url_normalizable("file://:555", 0);
	check_url_normalizable("scheme://user:pass@host", 1);
	check_url_normalizable("scheme://@host", 1);
	check_url_normalizable("scheme://%00@host", 1);
	check_url_normalizable("scheme://%%@host", 0);
	check_url_normalizable("scheme://host_", 1);
	check_url_normalizable("scheme://user:pass@host/", 1);
	check_url_normalizable("scheme://@host/", 1);
	check_url_normalizable("scheme://host/", 1);
	check_url_normalizable("scheme://host?x", 1);
	check_url_normalizable("scheme://host#x", 1);
	check_url_normalizable("scheme://host/@", 1);
	check_url_normalizable("scheme://host?@x", 1);
	check_url_normalizable("scheme://host#@x", 1);
	check_url_normalizable("scheme://[::1]", 1);
	check_url_normalizable("scheme://[::1]/", 1);
	check_url_normalizable("scheme://hos%41/", 0);
	check_url_normalizable("scheme://[invalid....:/", 1);
	check_url_normalizable("scheme://invalid....:]/", 1);
	check_url_normalizable("scheme://invalid....:[/", 0);
	check_url_normalizable("scheme://invalid....:[", 0);
}

static void t_url_port(void)
{
	check_url_normalizable("xyz://q@some.host:", 1);
	check_url_normalizable("xyz://q@some.host:456/", 1);
	check_url_normalizable("xyz://q@some.host:0", 0);
	check_url_normalizable("xyz://q@some.host:0000000", 0);
	check_url_normalizable("xyz://q@some.host:0000001?", 1);
	check_url_normalizable("xyz://q@some.host:065535#", 1);
	check_url_normalizable("xyz://q@some.host:65535", 1);
	check_url_normalizable("xyz://q@some.host:65536", 0);
	check_url_normalizable("xyz://q@some.host:99999", 0);
	check_url_normalizable("xyz://q@some.host:100000", 0);
	check_url_normalizable("xyz://q@some.host:100001", 0);
	check_url_normalizable("http://q@some.host:80", 1);
	check_url_normalizable("https://q@some.host:443", 1);
	check_url_normalizable("http://q@some.host:80/", 1);
	check_url_normalizable("https://q@some.host:443?", 1);
	check_url_normalizable("http://q@:8008", 0);
	check_url_normalizable("http://:8080", 0);
	check_url_normalizable("http://:", 0);
	check_url_normalizable("xyz://q@some.host:456/", 1);
	check_url_normalizable("xyz://[::1]:456/", 1);
	check_url_normalizable("xyz://[::1]:/", 1);
	check_url_normalizable("xyz://[::1]:000/", 0);
	check_url_normalizable("xyz://[::1]:0%300/", 0);
	check_url_normalizable("xyz://[::1]:0x80/", 0);
	check_url_normalizable("xyz://[::1]:4294967297/", 0);
	check_url_normalizable("xyz://[::1]:030f/", 0);
}

static void t_url_port_normalization(void)
{
	check_normalized_url("http://x:800", "http://x:800/");
	check_normalized_url("http://x:0800", "http://x:800/");
	check_normalized_url("http://x:00000800", "http://x:800/");
	check_normalized_url("http://x:065535", "http://x:65535/");
	check_normalized_url("http://x:1", "http://x:1/");
	check_normalized_url("http://x:80", "http://x/");
	check_normalized_url("http://x:080", "http://x/");
	check_normalized_url("http://x:000000080", "http://x/");
	check_normalized_url("https://x:443", "https://x/");
	check_normalized_url("https://x:0443", "https://x/");
	check_normalized_url("https://x:000000443", "https://x/");
}

static void t_url_general_escape(void)
{
	check_url_normalizable("http://x.y?%fg", 0);
	check_normalized_url("X://W/%7e%41^%3a", "x://w/~A%5E%3A");
	check_normalized_url("X://W/:/?#[]@", "x://w/:/?#[]@");
	check_normalized_url("X://W/$&()*+,;=", "x://w/$&()*+,;=");
	check_normalized_url("X://W/'", "x://w/'");
	check_normalized_url("X://W?!", "x://w/?!");
}

static void t_url_high_bit(void)
{
	check_normalized_url(
		"x://q/\x01\x02\x03\x04\x05\x06\x07\x08\x0e\x0f\x10\x11\x12",
		"x://q/%01%02%03%04%05%06%07%08%0E%0F%10%11%12");
	check_normalized_url(
		"x://q/\x13\x14\x15\x16\x17\x18\x19\x1b\x1c\x1d\x1e\x1f\x7f",
		"x://q/%13%14%15%16%17%18%19%1B%1C%1D%1E%1F%7F");
	check_normalized_url(
		"x://q/\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f",
		"x://q/%80%81%82%83%84%85%86%87%88%89%8A%8B%8C%8D%8E%8F");
	check_normalized_url(
		"x://q/\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f",
		"x://q/%90%91%92%93%94%95%96%97%98%99%9A%9B%9C%9D%9E%9F");
	check_normalized_url(
		"x://q/\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf",
		"x://q/%A0%A1%A2%A3%A4%A5%A6%A7%A8%A9%AA%AB%AC%AD%AE%AF");
	check_normalized_url(
		"x://q/\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf",
		"x://q/%B0%B1%B2%B3%B4%B5%B6%B7%B8%B9%BA%BB%BC%BD%BE%BF");
	check_normalized_url(
		"x://q/\xc0\xc1\xc2\xc3\xc4\xc5\xc6\xc7\xc8\xc9\xca\xcb\xcc\xcd\xce\xcf",
		"x://q/%C0%C1%C2%C3%C4%C5%C6%C7%C8%C9%CA%CB%CC%CD%CE%CF");
	check_normalized_url(
		"x://q/\xd0\xd1\xd2\xd3\xd4\xd5\xd6\xd7\xd8\xd9\xda\xdb\xdc\xdd\xde\xdf",
		"x://q/%D0%D1%D2%D3%D4%D5%D6%D7%D8%D9%DA%DB%DC%DD%DE%DF");
	check_normalized_url(
		"x://q/\xe0\xe1\xe2\xe3\xe4\xe5\xe6\xe7\xe8\xe9\xea\xeb\xec\xed\xee\xef",
		"x://q/%E0%E1%E2%E3%E4%E5%E6%E7%E8%E9%EA%EB%EC%ED%EE%EF");
	check_normalized_url(
		"x://q/\xf0\xf1\xf2\xf3\xf4\xf5\xf6\xf7\xf8\xf9\xfa\xfb\xfc\xfd\xfe\xff",
		"x://q/%F0%F1%F2%F3%F4%F5%F6%F7%F8%F9%FA%FB%FC%FD%FE%FF");
}

static void t_url_utf8_escape(void)
{
	check_normalized_url(
		"x://q/\xc2\x80\xdf\xbf\xe0\xa0\x80\xef\xbf\xbd\xf0\x90\x80\x80\xf0\xaf\xbf\xbd",
		"x://q/%C2%80%DF%BF%E0%A0%80%EF%BF%BD%F0%90%80%80%F0%AF%BF%BD");
}

static void t_url_username_pass(void)
{
	check_normalized_url("x://%41%62(^):%70+d@foo", "x://Ab(%5E):p+d@foo/");
}

static void t_url_length(void)
{
	check_normalized_url_length("Http://%4d%65:%4d^%70@The.Host", 25);
	check_normalized_url_length("http://%41:%42@x.y/%61/", 17);
	check_normalized_url_length("http://@x.y/^", 15);
}

static void t_url_dots(void)
{
	check_normalized_url("x://y/.", "x://y/");
	check_normalized_url("x://y/./", "x://y/");
	check_normalized_url("x://y/a/.", "x://y/a");
	check_normalized_url("x://y/a/./", "x://y/a/");
	check_normalized_url("x://y/.?", "x://y/?");
	check_normalized_url("x://y/./?", "x://y/?");
	check_normalized_url("x://y/a/.?", "x://y/a?");
	check_normalized_url("x://y/a/./?", "x://y/a/?");
	check_normalized_url("x://y/a/./b/.././../c", "x://y/c");
	check_normalized_url("x://y/a/./b/../.././c/", "x://y/c/");
	check_normalized_url("x://y/a/./b/.././../c/././.././.", "x://y/");
	check_url_normalizable("x://y/a/./b/.././../c/././.././..", 0);
	check_normalized_url("x://y/a/./?/././..", "x://y/a/?/././..");
	check_normalized_url("x://y/%2e/", "x://y/");
	check_normalized_url("x://y/%2E/", "x://y/");
	check_normalized_url("x://y/a/%2e./", "x://y/");
	check_normalized_url("x://y/b/.%2E/", "x://y/");
	check_normalized_url("x://y/c/%2e%2E/", "x://y/");
}

/*
 * "http://@foo" specifies an empty user name but does not specify a password.
 * "http://foo" specifies neither a user name nor a password.
 * So they should not be equivalent.
 */
static void t_url_equivalents(void)
{
	compare_normalized_urls("httP://x", "Http://X/", 1);
	compare_normalized_urls("Http://%4d%65:%4d^%70@The.Host", "hTTP://Me:%4D^p@the.HOST:80/", 1);
	compare_normalized_urls("https://@x.y/^", "httpS://x.y:443/^", 0);
	compare_normalized_urls("https://@x.y/^", "httpS://@x.y:0443/^", 1);
	compare_normalized_urls("https://@x.y/^/../abc", "httpS://@x.y:0443/abc", 1);
	compare_normalized_urls("https://@x.y/^/..", "httpS://@x.y:0443/", 1);
}

int cmd_main(int argc UNUSED, const char **argv UNUSED)
{
	TEST(t_url_scheme(), "url scheme");
	TEST(t_url_authority(), "url authority");
	TEST(t_url_port(), "url port checks");
	TEST(t_url_port_normalization(), "url port normalization");
	TEST(t_url_general_escape(), "url general escapes");
	TEST(t_url_high_bit(), "url high-bit escapes");
	TEST(t_url_utf8_escape(), "url utf8 escapes");
	TEST(t_url_username_pass(), "url username/password escapes");
	TEST(t_url_length(), "url normalized lengths");
	TEST(t_url_dots(), "url . and .. segments");
	TEST(t_url_equivalents(), "url equivalents");
	return test_done();
}
