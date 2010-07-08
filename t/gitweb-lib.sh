#!/bin/sh
#
# Copyright (c) 2007 Jakub Narebski
#

gitweb_init () {
	safe_pwd="$(perl -MPOSIX=getcwd -e 'print quotemeta(getcwd)')"
	cat >gitweb_config.perl <<EOF
#!/usr/bin/perl

# gitweb configuration for tests

our \$version = 'current';
our \$GIT = 'git';
our \$projectroot = "$safe_pwd";
our \$project_maxdepth = 8;
our \$home_link_str = 'projects';
our \$site_name = '[localhost]';
our \$site_header = '';
our \$site_footer = '';
our \$home_text = 'indextext.html';
our @stylesheets = ('file:///$TEST_DIRECTORY/../gitweb/static/gitweb.css');
our \$logo = 'file:///$TEST_DIRECTORY/../gitweb/static/git-logo.png';
our \$favicon = 'file:///$TEST_DIRECTORY/../gitweb/static/git-favicon.png';
our \$projects_list = '';
our \$export_ok = '';
our \$strict_export = '';
our \$maxload = undef;

EOF

	cat >.git/description <<EOF
$0 test repository
EOF
}

gitweb_run () {
	GATEWAY_INTERFACE='CGI/1.1'
	HTTP_ACCEPT='*/*'
	REQUEST_METHOD='GET'
	SCRIPT_NAME="$TEST_DIRECTORY/../gitweb/gitweb.perl"
	QUERY_STRING=""$1""
	PATH_INFO=""$2""
	export GATEWAY_INTERFACE HTTP_ACCEPT REQUEST_METHOD \
		SCRIPT_NAME QUERY_STRING PATH_INFO

	GITWEB_CONFIG=$(pwd)/gitweb_config.perl
	export GITWEB_CONFIG

	# some of git commands write to STDERR on error, but this is not
	# written to web server logs, so we are not interested in that:
	# we are interested only in properly formatted errors/warnings
	rm -f gitweb.log &&
	perl -- "$SCRIPT_NAME" \
		>gitweb.output 2>gitweb.log &&
	perl -w -e '
		open O, ">gitweb.headers";
		while (<>) {
			print O;
			last if (/^\r$/ || /^$/);
		}
		open O, ">gitweb.body";
		while (<>) {
			print O;
		}
		close O;
	' gitweb.output &&
	if grep '^[[]' gitweb.log >/dev/null 2>&1; then false; else true; fi

	# gitweb.log is left for debugging
	# gitweb.output is used to parse HTTP output
	# gitweb.headers contains only HTTP headers
	# gitweb.body contains body of message, without headers
}

. ./test-lib.sh

if ! test_have_prereq PERL; then
	skip_all='skipping gitweb tests, perl not available'
	test_done
fi

perl -MEncode -e 'decode_utf8("", Encode::FB_CROAK)' >/dev/null 2>&1 || {
    skip_all='skipping gitweb tests, perl version is too old'
    test_done
}

gitweb_init
