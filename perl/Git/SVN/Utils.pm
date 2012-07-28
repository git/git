package Git::SVN::Utils;

use strict;
use warnings;

use base qw(Exporter);

our @EXPORT_OK = qw(fatal can_compress);


=head1 NAME

Git::SVN::Utils - utility functions used across Git::SVN

=head1 SYNOPSIS

    use Git::SVN::Utils qw(functions to import);

=head1 DESCRIPTION

This module contains functions which are useful across many different
parts of Git::SVN.  Mostly it's a place to put utility functions
rather than duplicate the code or have classes grabbing at other
classes.

=head1 FUNCTIONS

All functions can be imported only on request.

=head3 fatal

    fatal(@message);

Display a message and exit with a fatal error code.

=cut

# Note: not certain why this is in use instead of die.  Probably because
# the exit code of die is 255?  Doesn't appear to be used consistently.
sub fatal (@) { print STDERR "@_\n"; exit 1 }


=head3 can_compress

    my $can_compress = can_compress;

Returns true if Compress::Zlib is available, false otherwise.

=cut

my $can_compress;
sub can_compress {
	return $can_compress if defined $can_compress;

	return $can_compress = eval { require Compress::Zlib; };
}


1;
