# Initialization and helpers for Gitweb tests, which source this
# shell library instead of test-lib.sh.
#
# Copyright (c) 2007 Jakub Narebski
#

butweb_init () {
	safe_pwd="$(perl -MPOSIX=getcwd -e 'print quotemeta(getcwd)')"
	cat >butweb_config.perl <<EOF
#!/usr/bin/perl

# butweb configuration for tests

our \$version = 'current';
our \$GIT = 'but';
our \$projectroot = "$safe_pwd";
our \$project_maxdepth = 8;
our \$home_link_str = 'projects';
our \$site_name = '[localhost]';
our \$site_html_head_string = '';
our \$site_header = '';
our \$site_footer = '';
our \$home_text = 'indextext.html';
our @stylesheets = ('file:///$GIT_BUILD_DIR/butweb/static/butweb.css');
our \$logo = 'file:///$GIT_BUILD_DIR/butweb/static/but-logo.png';
our \$favicon = 'file:///$GIT_BUILD_DIR/butweb/static/but-favicon.png';
our \$projects_list = '';
our \$export_ok = '';
our \$strict_export = '';
our \$maxload = undef;

EOF

	cat >.but/description <<EOF
$0 test repository
EOF

	# You can set the GITWEB_TEST_INSTALLED environment variable to
	# the butwebdir (the directory where butweb is installed / deployed to)
	# of an existing butweb installation to test that installation,
	# or simply to pathname of installed butweb script.
	if test -n "$GITWEB_TEST_INSTALLED" ; then
		if test -d $GITWEB_TEST_INSTALLED; then
			SCRIPT_NAME="$GITWEB_TEST_INSTALLED/butweb.cgi"
		else
			SCRIPT_NAME="$GITWEB_TEST_INSTALLED"
		fi
		test -f "$SCRIPT_NAME" ||
		error "Cannot find butweb at $GITWEB_TEST_INSTALLED."
		say "# Testing $SCRIPT_NAME"
	else # normal case, use source version of butweb
		SCRIPT_NAME="$GIT_BUILD_DIR/butweb/butweb.perl"
	fi
	export SCRIPT_NAME
}

butweb_run () {
	GATEWAY_INTERFACE='CGI/1.1'
	HTTP_ACCEPT='*/*'
	REQUEST_METHOD='GET'
	QUERY_STRING=$1
	PATH_INFO=$2
	REQUEST_URI=/butweb.cgi$PATH_INFO
	export GATEWAY_INTERFACE HTTP_ACCEPT REQUEST_METHOD \
		QUERY_STRING PATH_INFO REQUEST_URI

	GITWEB_CONFIG=$(pwd)/butweb_config.perl
	export GITWEB_CONFIG

	# some of but commands write to STDERR on error, but this is not
	# written to web server logs, so we are not interested in that:
	# we are interested only in properly formatted errors/warnings
	rm -f butweb.log &&
	perl -- "$SCRIPT_NAME" \
		>butweb.output 2>butweb.log &&
	perl -w -e '
		open O, ">butweb.headers";
		while (<>) {
			print O;
			last if (/^\r$/ || /^$/);
		}
		open O, ">butweb.body";
		while (<>) {
			print O;
		}
		close O;
	' butweb.output &&
	if grep '^[[]' butweb.log >/dev/null 2>&1; then
		test_debug 'cat butweb.log >&2' &&
		false
	else
		true
	fi

	# butweb.log is left for debugging
	# butweb.output is used to parse HTTP output
	# butweb.headers contains only HTTP headers
	# butweb.body contains body of message, without headers
}

. ./test-lib.sh

if ! test_have_prereq PERL; then
	skip_all='skipping butweb tests, perl not available'
	test_done
fi

perl -MEncode -e '$e="";decode_utf8($e, Encode::FB_CROAK)' >/dev/null 2>&1 || {
	skip_all='skipping butweb tests, perl version is too old'
	test_done
}

perl -MCGI -MCGI::Util -MCGI::Carp -e 0 >/dev/null 2>&1 || {
	skip_all='skipping butweb tests, CGI & CGI::Util & CGI::Carp modules not available'
	test_done
}

perl -mTime::HiRes -e 0 >/dev/null 2>&1 || {
	skip_all='skipping butweb tests, Time::HiRes module not available'
	test_done
}

butweb_init
