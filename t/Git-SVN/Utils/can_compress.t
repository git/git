#!/usr/bin/perl

use strict;
use warnings;

use Test::More 'no_plan';

use Git::SVN::Utils qw(can_compress);

# !! is the "convert this to boolean" operator.
is !!can_compress(), !!eval { require Compress::Zlib };
