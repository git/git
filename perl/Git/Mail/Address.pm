package Git::Mail::Address;
use 5.008;
use strict;
use warnings;

=head1 NAME

Git::Mail::Address - Wrapper for the L<Mail::Address> module, in case it's not installed

=head1 DESCRIPTION

This module is only intended to be used for code shipping in the
C<git.git> repository. Use it for anything else at your peril!

=cut

eval {
    require Mail::Address;
    1;
} or do {
    require Git::FromCPAN::Mail::Address;
};

1;
