. ./test-lib.sh

if test -n "$NO_SVN_TESTS"
then
	skip_all='skipping git svn tests, NO_SVN_TESTS defined'
	test_done
fi
if ! test_have_prereq PERL; then
	skip_all='skipping git svn tests, perl not available'
	test_done
fi

GIT_DIR=$PWD/.git
GIT_SVN_DIR=$GIT_DIR/svn/refs/remotes/git-svn
SVN_TREE=$GIT_SVN_DIR/svn-tree

svn >/dev/null 2>&1
if test $? -ne 1
then
    skip_all='skipping git svn tests, svn not found'
    test_done
fi

svnrepo=$PWD/svnrepo
export svnrepo
svnconf=$PWD/svnconf
export svnconf

perl -w -e "
use SVN::Core;
use SVN::Repos;
\$SVN::Core::VERSION gt '1.1.0' or exit(42);
system(qw/svnadmin create --fs-type fsfs/, \$ENV{svnrepo}) == 0 or exit(41);
" >&3 2>&4
x=$?
if test $x -ne 0
then
	if test $x -eq 42; then
		skip_all='Perl SVN libraries must be >= 1.1.0'
	elif test $x -eq 41; then
		skip_all='svnadmin failed to create fsfs repository'
	else
		skip_all='Perl SVN libraries not found or unusable'
	fi
	test_done
fi

rawsvnrepo="$svnrepo"
svnrepo="file://$svnrepo"

poke() {
	test-chmtime +1 "$1"
}

# We need this, because we should pass empty configuration directory to
# the 'svn commit' to avoid automated property changes and other stuff
# that could be set from user's configuration files in ~/.subversion.
svn_cmd () {
	[ -d "$svnconf" ] || mkdir "$svnconf"
	orig_svncmd="$1"; shift
	if [ -z "$orig_svncmd" ]; then
		svn
		return
	fi
	svn "$orig_svncmd" --config-dir "$svnconf" "$@"
}

maybe_start_httpd () {
	loc=${1-svn}

	test_tristate GIT_SVN_TEST_HTTPD
	case $GIT_SVN_TEST_HTTPD in
	true)
		. "$TEST_DIRECTORY"/lib-httpd.sh
		LIB_HTTPD_SVN="$loc"
		start_httpd
		;;
	*)
		stop_httpd () {
			: noop
		}
		;;
	esac
}

convert_to_rev_db () {
	perl -w -- - "$@" <<\EOF
use strict;
@ARGV == 2 or die "usage: convert_to_rev_db <input> <output>";
open my $wr, '+>', $ARGV[1] or die "$!: couldn't open: $ARGV[1]";
open my $rd, '<', $ARGV[0] or die "$!: couldn't open: $ARGV[0]";
my $size = (stat($rd))[7];
($size % 24) == 0 or die "Inconsistent size: $size";
while (sysread($rd, my $buf, 24) == 24) {
	my ($r, $c) = unpack('NH40', $buf);
	my $offset = $r * 41;
	seek $wr, 0, 2 or die $!;
	my $pos = tell $wr;
	if ($pos < $offset) {
		for (1 .. (($offset - $pos) / 41)) {
			print $wr (('0' x 40),"\n") or die $!;
		}
	}
	seek $wr, $offset, 0 or die $!;
	print $wr $c,"\n" or die $!;
}
close $wr or die $!;
close $rd or die $!;
EOF
}

require_svnserve () {
    if test -z "$SVNSERVE_PORT"
    then
	skip_all='skipping svnserve test. (set $SVNSERVE_PORT to enable)'
        test_done
    fi
}

start_svnserve () {
    svnserve --listen-port $SVNSERVE_PORT \
             --root "$rawsvnrepo" \
             --listen-once \
             --listen-host 127.0.0.1 &
}

prepare_a_utf8_locale () {
	a_utf8_locale=$(locale -a | sed -n '/\.[uU][tT][fF]-*8$/{
	p
	q
}')
	if test -n "$a_utf8_locale"
	then
		test_set_prereq UTF8
	else
		say "# UTF-8 locale not available, some tests are skipped"
	fi
}
