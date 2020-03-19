package Git::SVN::Memoize::YAML;
use warnings;
use strict;
use YAML::Any ();

# based on Memoize::Storable.

sub TIEHASH {
	my $package = shift;
	my $filename = shift;
	my $truehash = (-e $filename) ? YAML::Any::LoadFile($filename) : {};
	my $self = {FILENAME => $filename, H => $truehash};
	bless $self => $package;
}

sub STORE {
	my $self = shift;
	$self->{H}{$_[0]} = $_[1];
}

sub FETCH {
	my $self = shift;
	$self->{H}{$_[0]};
}

sub EXISTS {
	my $self = shift;
	exists $self->{H}{$_[0]};
}

sub DESTROY {
	my $self = shift;
	YAML::Any::DumpFile($self->{FILENAME}, $self->{H});
}

sub SCALAR {
	my $self = shift;
	scalar(%{$self->{H}});
}

sub FIRSTKEY {
	'Fake hash from Git::SVN::Memoize::YAML';
}

sub NEXTKEY {
	undef;
}

1;
__END__

=head1 NAME

Git::SVN::Memoize::YAML - store Memoized data in YAML format

=head1 SYNOPSIS

    use Memoize;
    use Git::SVN::Memoize::YAML;

    tie my %cache => 'Git::SVN::Memoize::YAML', $filename;
    memoize('slow_function', SCALAR_CACHE => [HASH => \%cache]);
    slow_function(arguments);

=head1 DESCRIPTION

This module provides a class that can be used to tie a hash to a
YAML file.  The file is read when the hash is initialized and
rewritten when the hash is destroyed.

The intent is to allow L<Memoize> to back its cache with a file in
YAML format, just like L<Memoize::Storable> allows L<Memoize> to
back its cache with a file in Storable format.  Unlike the Storable
format, the YAML format is platform-independent and fairly stable.

Carps on error.

=head1 DIAGNOSTICS

See L<YAML::Any>.

=head1 DEPENDENCIES

L<YAML::Any> from CPAN.

=head1 INCOMPATIBILITIES

None reported.

=head1 BUGS

The entire cache is read into a Perl hash when loading the file,
so this is not very scalable.
