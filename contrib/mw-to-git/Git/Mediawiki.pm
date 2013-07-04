package Git::Mediawiki;

use 5.008;
use strict;
use Git;

BEGIN {

our ($VERSION, @ISA, @EXPORT, @EXPORT_OK);

# Totally unstable API.
$VERSION = '0.01';

require Exporter;

@ISA = qw(Exporter);

@EXPORT = ();

# Methods which can be called as standalone functions as well:
@EXPORT_OK = ();
}

1; # Famous last words
