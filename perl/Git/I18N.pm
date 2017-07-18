package Git::I18N;
use 5.008;
use strict;
use warnings;
BEGIN {
	require Exporter;
	if ($] < 5.008003) {
		*import = \&Exporter::import;
	} else {
		# Exporter 5.57 which supports this invocation was
		# released with perl 5.8.3
		Exporter->import('import');
	}
}

our @EXPORT = qw(__);
our @EXPORT_OK = @EXPORT;

sub __bootstrap_locale_messages {
	our $TEXTDOMAIN = 'git';
	our $TEXTDOMAINDIR = $ENV{GIT_TEXTDOMAINDIR} || '++LOCALEDIR++';

	require POSIX;
	POSIX->import(qw(setlocale));
	# Non-core prerequisite module
	require Locale::Messages;
	Locale::Messages->import(qw(:locale_h :libintl_h));

	setlocale(LC_MESSAGES(), '');
	setlocale(LC_CTYPE(), '');
	textdomain($TEXTDOMAIN);
	bindtextdomain($TEXTDOMAIN => $TEXTDOMAINDIR);

	return;
}

BEGIN
{
	# Used by our test script to see if it should test fallbacks or
	# not.
	our $__HAS_LIBRARY = 1;

	local $@;
	eval {
		__bootstrap_locale_messages();
		*__ = \&Locale::Messages::gettext;
		1;
	} or do {
		# Tell test.pl that we couldn't load the gettext library.
		$Git::I18N::__HAS_LIBRARY = 0;

		# Just a fall-through no-op
		*__ = sub ($) { $_[0] };
	};
}

1;

__END__

=head1 NAME

Git::I18N - Perl interface to Git's Gettext localizations

=head1 SYNOPSIS

	use Git::I18N;

	print __("Welcome to Git!\n");

	printf __("The following error occurred: %s\n"), $error;

=head1 DESCRIPTION

Git's internal Perl interface to gettext via L<Locale::Messages>. If
L<Locale::Messages> can't be loaded (it's not a core module) we
provide stub passthrough fallbacks.

This is a distilled interface to gettext, see C<info '(gettext)Perl'>
for the full interface. This module implements only a small part of
it.

=head1 FUNCTIONS

=head2 __($)

L<Locale::Messages>'s gettext function if all goes well, otherwise our
passthrough fallback function.

=head1 AUTHOR

E<AElig>var ArnfjE<ouml>rE<eth> Bjarmason <avarab@gmail.com>

=head1 COPYRIGHT

Copyright 2010 E<AElig>var ArnfjE<ouml>rE<eth> Bjarmason <avarab@gmail.com>

=cut
