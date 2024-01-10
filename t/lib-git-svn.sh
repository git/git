if test -z "$TEST_FAILS_SANITIZE_LEAK"
then
	TEST_PASSES_SANITIZE_LEAK=true
fi
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
test_set_port SVNSERVE_PORT

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
	test-tool chmtime +1 "$1"
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

	if test_bool_env GIT_TEST_SVN_HTTPD false
	then
		. "$TEST_DIRECTORY"/lib-httpd.sh
		LIB_HTTPD_SVN="$loc"
		start_httpd
	fi
}

convert_to_rev_db () {
	perl -w -- - "$(test_oid rawsz)" "$@" <<\EOF
use strict;
my $oidlen = shift;
@ARGV == 2 or die "usage: convert_to_rev_db <input> <output>";
my $record_size = $oidlen + 4;
my $hexlen = $oidlen * 2;
open my $wr, '+>', $ARGV[1] or die "$!: couldn't open: $ARGV[1]";
open my $rd, '<', $ARGV[0] or die "$!: couldn't open: $ARGV[0]";
my $size = (stat($rd))[7];
($size % $record_size) == 0 or die "Inconsistent size: $size";
while (sysread($rd, my $buf, $record_size) == $record_size) {
	my ($r, $c) = unpack("NH$hexlen", $buf);
	my $offset = $r * ($hexlen + 1);
	seek $wr, 0, 2 or die $!;
	my $pos = tell $wr;
	if ($pos < $offset) {
		for (1 .. (($offset - $pos) / ($hexlen + 1))) {
			print $wr (('0' x $hexlen),"\n") or die $!;
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
	if ! test_bool_env GIT_TEST_SVNSERVE false
	then
		skip_all='skipping svnserve test. (set $GIT_TEST_SVNSERVE to enable)'
		test_done
	fi
}

start_svnserve () {
	svnserve --listen-port $SVNSERVE_PORT \
		 --root "$rawsvnrepo" \
		 --listen-once \
		 --listen-host 127.0.0.1 &
}

prepare_utf8_locale () {
	if test -z "$GIT_TEST_UTF8_LOCALE"
	then
		case "${LC_ALL:-$LANG}" in
		*.[Uu][Tt][Ff]8 | *.[Uu][Tt][Ff]-8)
			GIT_TEST_UTF8_LOCALE="${LC_ALL:-$LANG}"
			;;
		*)
			GIT_TEST_UTF8_LOCALE=$(locale -a | sed -n '/\.[uU][tT][fF]-*8$/{
				p
				q
			}')
			;;
		esac
	fi
	if test -n "$GIT_TEST_UTF8_LOCALE"
	then
		test_set_prereq UTF8
	else
		say "# UTF-8 locale not available, some tests are skipped"
	fi
}
