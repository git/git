#!/usr/bin/env perl

use strict;
use warnings;

use Test::More tests => 5;

require_ok 'Git::SVN';
require_ok 'Git::SVN::Utils';
require_ok 'Git::SVN::Ra';
require_ok 'Git::SVN::Log';
require_ok 'Git::SVN::Migration';
