package Git::SVN::Utils;

use strict;
use warnings;

use SVN::Core;

use base qw(Exporter);

our @EXPORT_OK = qw(
	fatal
	can_compress
	canonicalize_path
	canonicalize_url
	join_paths
);


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


=head3 canonicalize_path

    my $canoncalized_path = canonicalize_path($path);

Converts $path into a canonical form which is safe to pass to the SVN
API as a file path.

=cut

# Turn foo/../bar into bar
sub _collapse_dotdot {
	my $path = shift;

	1 while $path =~ s{/[^/]+/+\.\.}{};
	1 while $path =~ s{[^/]+/+\.\./}{};
	1 while $path =~ s{[^/]+/+\.\.}{};

	return $path;
}


sub canonicalize_path {
	my ($path) = @_;
	my $dot_slash_added = 0;
	if (substr($path, 0, 1) ne "/") {
		$path = "./" . $path;
		$dot_slash_added = 1;
	}
	$path =~ s#/+#/#g;
	$path =~ s#/\.(?:/|$)#/#g;
	$path = _collapse_dotdot($path);
	$path =~ s#/$##g;
	$path =~ s#^\./## if $dot_slash_added;
	$path =~ s#^/##;
	$path =~ s#^\.$##;
	return $path;
}


=head3 canonicalize_url

    my $canonicalized_url = canonicalize_url($url);

Converts $url into a canonical form which is safe to pass to the SVN
API as a URL.

=cut

sub canonicalize_url {
	my $url = shift;

	# The 1.7 way to do it
	if ( defined &SVN::_Core::svn_uri_canonicalize ) {
		return SVN::_Core::svn_uri_canonicalize($url);
	}
	# There wasn't a 1.6 way to do it, so we do it ourself.
	else {
		return _canonicalize_url_ourselves($url);
	}
}


sub _canonicalize_url_ourselves {
	my ($url) = @_;
	$url =~ s#^([^:]+://[^/]*/)(.*)$#$1 . canonicalize_path($2)#e;
	return $url;
}


=head3 join_paths

    my $new_path = join_paths(@paths);

Appends @paths together into a single path.  Any empty paths are ignored.

=cut

sub join_paths {
	my @paths = @_;

	@paths = grep { defined $_ && length $_ } @paths;

	return '' unless @paths;
	return $paths[0] if @paths == 1;

	my $new_path = shift @paths;
	$new_path =~ s{/+$}{};

	my $last_path = pop @paths;
	$last_path =~ s{^/+}{};

	for my $path (@paths) {
		$path =~ s{^/+}{};
		$path =~ s{/+$}{};
		$new_path .= "/$path";
	}

	return $new_path .= "/$last_path";
}

1;
