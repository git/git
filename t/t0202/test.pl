#!/usr/bin/perl
use 5.008;
use lib (split(/:/, $ENV{GITPERLLIB}));
use strict;
use warnings;
use POSIX qw(:locale_h);
use Test::More tests => 8;
use Git::I18N;

my $has_gettext_library = $Git::I18N::__HAS_LIBRARY;

ok(1, "Testing Git::I18N with " .
	 ($has_gettext_library
	  ? (defined $Locale::Messages::VERSION
		 ? "Locale::Messages version $Locale::Messages::VERSION"
		 # Versions of Locale::Messages before 1.17 didn't have a
		 # $VERSION variable.
		 : "Locale::Messages version <1.17")
	  : "NO Perl gettext library"));
ok(1, "Git::I18N is located at $INC{'Git/I18N.pm'}");

{
	my $exports = @Git::I18N::EXPORT;
	ok($exports, "sanity: Git::I18N has $exports export(s)");
}
is_deeply(\@Git::I18N::EXPORT, \@Git::I18N::EXPORT_OK, "sanity: Git::I18N exports everything by default");

# prototypes
{
	# Add prototypes here when modifying the public interface to add
	# more gettext wrapper functions.
	my %prototypes = (qw(
		__	$
	));
	while (my ($sub, $proto) = each %prototypes) {
		is(prototype(\&{"Git::I18N::$sub"}), $proto, "sanity: $sub has a $proto prototype");
	}
}

# Test basic passthrough in the C locale
{
	local $ENV{LANGUAGE} = 'C';
	local $ENV{LC_ALL}   = 'C';
	local $ENV{LANG}     = 'C';

	my ($got, $expect) = (('TEST: A Perl test string') x 2);

	is(__($got), $expect, "Passing a string through __() in the C locale works");
}

# Test a basic message on different locales
SKIP: {
	unless ($ENV{GETTEXT_LOCALE}) {
		# Can't reliably test __() with a non-C locales because the
		# required locales may not be installed on the system.
		#
		# We test for these anyway as part of the shell
		# tests. Skipping these here will eliminate failures on odd
		# platforms with incomplete locale data.

		skip "GETTEXT_LOCALE must be set by lib-gettext.sh for exhaustive Git::I18N tests", 2;
	}

	# The is_IS UTF-8 locale passed from lib-gettext.sh
	my $is_IS_locale = $ENV{is_IS_locale};

	my $test = sub {
		my ($got, $expect, $msg, $locale) = @_;
		# Maybe this system doesn't have the locale we're trying to
		# test.
		my $locale_ok = setlocale(LC_ALL, $locale);
		is(__($got), $expect, "$msg a gettext library + <$locale> locale <$got> turns into <$expect>");
	};

	my $env_C = sub {
		$ENV{LANGUAGE} = 'C';
		$ENV{LC_ALL}   = 'C';
	};

	my $env_is = sub {
		$ENV{LANGUAGE} = 'is';
		$ENV{LC_ALL}   = $is_IS_locale;
	};

	# Translation's the same as the original
	my ($got, $expect) = (('TEST: A Perl test string') x 2);

	if ($has_gettext_library) {
		{
			local %ENV; $env_C->();
			$test->($got, $expect, "With", 'C');
		}

		{
			my ($got, $expect) = ($got, 'TILRAUN: Perl tilraunastrengur');
			local %ENV; $env_is->();
			$test->($got, $expect, "With", $is_IS_locale);
		}
	} else {
		{
			local %ENV; $env_C->();
			$test->($got, $expect, "Without", 'C');
		}

		{
			local %ENV; $env_is->();
			$test->($got, $expect, "Without", 'is');
		}
	}
}
