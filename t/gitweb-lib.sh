# Initialization and helpers for Gitweb tests, which source this
# shell library instead of test-lib.sh.
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
our \$site_html_head_string = '';
our \$site_header = '';
our \$site_footer = '';
our \$home_text = 'indextext.html';
our @stylesheets = ('file:///$GIT_BUILD_DIR/gitweb/static/gitweb.css');
our \$logo = 'file:///$GIT_BUILD_DIR/gitweb/static/git-logo.png';
our \$favicon = 'file:///$GIT_BUILD_DIR/gitweb/static/git-favicon.png';
our \$projects_list = '';
our \$export_ok = '';
our \$strict_export = '';
our \$maxload = undef;

EOF

	cat >.git/description <<EOF
$0 test repository
EOF

	# You can set the GITWEB_TEST_INSTALLED environment variable to
	# the gitwebdir (the directory where gitweb is installed / deployed to)
	# of an existing gitweb installation to test that installation,
	# or simply to pathname of installed gitweb script.
	if test -n "$GITWEB_TEST_INSTALLED" ; then
		if test -d $GITWEB_TEST_INSTALLED; then
			SCRIPT_NAME="$GITWEB_TEST_INSTALLED/gitweb.cgi"
		else
			SCRIPT_NAME="$GITWEB_TEST_INSTALLED"
		fi
		test -f "$SCRIPT_NAME" ||
		error "Cannot find gitweb at $GITWEB_TEST_INSTALLED."
		say "# Testing $SCRIPT_NAME"
	else # normal case, use source version of gitweb
		SCRIPT_NAME="$GIT_BUILD_DIR/gitweb/gitweb.perl"
	fi
	export SCRIPT_NAME
}

gitweb_run () {
	GATEWAY_INTERFACE='CGI/1.1'
	HTTP_ACCEPT='*/*'
	REQUEST_METHOD='GET'
	QUERY_STRING=""$1""
	PATH_INFO=""$2""
	export GATEWAY_INTERFACE HTTP_ACCEPT REQUEST_METHOD \
		QUERY_STRING PATH_INFO

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
	if grep '^[[]' gitweb.log >/dev/null 2>&1; then
		test_debug 'cat gitweb.log >&2' &&
		false
	else
		true
	fi

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

perl -MEncode -e '$e="";decode_utf8($e, Encode::FB_CROAK)' >/dev/null 2>&1 || {
	skip_all='skipping gitweb tests, perl version is too old'
	test_done
}

perl -MCGI -MCGI::Util -MCGI::Carp -e 0 >/dev/null 2>&1 || {
	skip_all='skipping gitweb tests, CGI & CGI::Util & CGI::Carp modules not available'
	test_done
}

perl -mTime::HiRes -e 0 >/dev/null 2>&1 || {
	skip_all='skipping gitweb tests, Time::HiRes module not available'
	test_done
}

gitweb_init
