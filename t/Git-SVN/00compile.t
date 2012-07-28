#!/usr/bin/env perl

use strict;
use warnings;

use Test::More tests => 7;

require_ok 'Git::SVN';
require_ok 'Git::SVN::Utils';
require_ok 'Git::SVN::Ra';
require_ok 'Git::SVN::Log';
require_ok 'Git::SVN::Migration';
require_ok 'Git::IndexInfo';
require_ok 'Git::SVN::GlobSpec';
