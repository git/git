#!/bin/sh
#
# Copyright (c) 2007 Johannes E. Schindelin
#

test_description='Test custom diff function name patterns'

. ./test-lib.sh

LF='
'
cat >Beer.java <<\EOF
public class Beer
{
	int special;
	public static void main(String args[])
	{
		String s=" ";
		for(int x = 99; x > 0; x--)
		{
			System.out.print(x + " bottles of beer on the wall "
				+ x + " bottles of beer\n"
				+ "Take one down, pass it around, " + (x - 1)
				+ " bottles of beer on the wall.\n");
		}
		System.out.print("Go to the store, buy some more,\n"
			+ "99 bottles of beer on the wall.\n");
	}
}
EOF
sed 's/beer\\/beer,\\/' <Beer.java >Beer-correct.java
cat >Beer.perl <<\EOT
package Beer;

use strict;
use warnings;
use parent qw(Exporter);
our @EXPORT_OK = qw(round finalround);

sub other; # forward declaration

# hello

sub round {
	my ($n) = @_;
	print "$n bottles of beer on the wall ";
	print "$n bottles of beer\n";
	print "Take one down, pass it around, ";
	$n = $n - 1;
	print "$n bottles of beer on the wall.\n";
}

sub finalround
{
	print "Go to the store, buy some more\n";
	print "99 bottles of beer on the wall.\n");
}

sub withheredocument {
	print <<"EOF"
decoy here-doc
EOF
	# some lines of context
	# to pad it out
	print "hello\n";
}

__END__

=head1 NAME

Beer - subroutine to output fragment of a drinking song

=head1 SYNOPSIS

	use Beer qw(round finalround);

	sub song {
		for (my $i = 99; $i > 0; $i--) {
			round $i;
		}
		finalround;
	}

	song;

=cut
EOT
sed -e '
	s/hello/goodbye/
	s/beer\\/beer,\\/
	s/more\\/more,\\/
	s/song;/song();/
' <Beer.perl >Beer-correct.perl

test_config () {
	git config "$1" "$2" &&
	test_when_finished "git config --unset $1"
}

test_expect_funcname () {
	lang=${2-java}
	test_expect_code 1 git diff --no-index -U1 \
		"Beer.$lang" "Beer-correct.$lang" >diff &&
	grep "^@@.*@@ $1" diff
}

for p in bibtex cpp csharp fortran html java objc pascal perl php python ruby tex
do
	test_expect_success "builtin $p pattern compiles" '
		echo "*.java diff=$p" >.gitattributes &&
		test_expect_code 1 git diff --no-index \
			Beer.java Beer-correct.java 2>msg &&
		! grep fatal msg &&
		! grep error msg
	'
	test_expect_success "builtin $p wordRegex pattern compiles" '
		echo "*.java diff=$p" >.gitattributes &&
		test_expect_code 1 git diff --no-index --word-diff \
			Beer.java Beer-correct.java 2>msg &&
		! grep fatal msg &&
		! grep error msg
	'
done

test_expect_success 'default behaviour' '
	rm -f .gitattributes &&
	test_expect_funcname "public class Beer\$"
'

test_expect_success 'set up .gitattributes declaring drivers to test' '
	cat >.gitattributes <<-\EOF
	*.java diff=java
	*.perl diff=perl
	EOF
'

test_expect_success 'preset java pattern' '
	test_expect_funcname "public static void main("
'

test_expect_success 'preset perl pattern' '
	test_expect_funcname "sub round {\$" perl
'

test_expect_success 'perl pattern accepts K&R style brace placement, too' '
	test_expect_funcname "sub finalround\$" perl
'

test_expect_success 'but is not distracted by end of <<here document' '
	test_expect_funcname "sub withheredocument {\$" perl
'

test_expect_success 'perl pattern is not distracted by sub within POD' '
	test_expect_funcname "=head" perl
'

test_expect_success 'perl pattern gets full line of POD header' '
	test_expect_funcname "=head1 SYNOPSIS\$" perl
'

test_expect_success 'perl pattern is not distracted by forward declaration' '
	test_expect_funcname "package Beer;\$" perl
'

test_expect_success 'custom pattern' '
	test_config diff.java.funcname "!static
!String
[^ 	].*s.*" &&
	test_expect_funcname "int special;\$"
'

test_expect_success 'last regexp must not be negated' '
	test_config diff.java.funcname "!static" &&
	test_expect_code 128 git diff --no-index Beer.java Beer-correct.java 2>msg &&
	grep ": Last expression must not be negated:" msg
'

test_expect_success 'pattern which matches to end of line' '
	test_config diff.java.funcname "Beer\$" &&
	test_expect_funcname "Beer\$"
'

test_expect_success 'alternation in pattern' '
	test_config diff.java.funcname "Beer$" &&
	test_config diff.java.xfuncname "^[ 	]*((public|static).*)$" &&
	test_expect_funcname "public static void main("
'

test_done
