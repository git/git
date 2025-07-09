package Git::LoadCPAN::Error;
use 5.008001;
use strict;
use warnings $ENV{GIT_PERL_FATAL_WARNINGS} ? qw(FATAL all) : ();
use Git::LoadCPAN (
	module => 'Error',
	import => 1,
);

1;
