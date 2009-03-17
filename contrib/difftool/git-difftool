#!/usr/bin/env perl
# Copyright (c) 2009 David Aguilar
#
# This is a wrapper around the GIT_EXTERNAL_DIFF-compatible
# git-difftool-helper script.  This script exports
# GIT_EXTERNAL_DIFF and GIT_PAGER for use by git, and
# GIT_DIFFTOOL_NO_PROMPT and GIT_DIFF_TOOL for use by git-difftool-helper.
# Any arguments that are unknown to this script are forwarded to 'git diff'.

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Basename qw(dirname);

my $DIR = abs_path(dirname($0));


sub usage
{
	print << 'USAGE';
usage: git difftool [--tool=<tool>] [--no-prompt] ["git diff" options]
USAGE
	exit 1;
}

sub setup_environment
{
	$ENV{PATH} = "$DIR:$ENV{PATH}";
	$ENV{GIT_PAGER} = '';
	$ENV{GIT_EXTERNAL_DIFF} = 'git-difftool-helper';
}

sub exe
{
	my $exe = shift;
	return defined $ENV{COMSPEC} ? "$exe.exe" : $exe;
}

sub generate_command
{
	my @command = (exe('git'), 'diff');
	my $skip_next = 0;
	my $idx = -1;
	for my $arg (@ARGV) {
		$idx++;
		if ($skip_next) {
			$skip_next = 0;
			next;
		}
		if ($arg eq '-t' or $arg eq '--tool') {
			usage() if $#ARGV <= $idx;
			$ENV{GIT_DIFF_TOOL} = $ARGV[$idx + 1];
			$skip_next = 1;
			next;
		}
		if ($arg =~ /^--tool=/) {
			$ENV{GIT_DIFF_TOOL} = substr($arg, 7);
			next;
		}
		if ($arg eq '--no-prompt') {
			$ENV{GIT_DIFFTOOL_NO_PROMPT} = 'true';
			next;
		}
		if ($arg eq '-h' or $arg eq '--help') {
			usage();
		}
		push @command, $arg;
	}
	return @command
}

setup_environment();
exec(generate_command());
