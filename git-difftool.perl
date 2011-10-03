#!/usr/bin/env perl
# Copyright (c) 2009, 2010 David Aguilar
#
# This is a wrapper around the GIT_EXTERNAL_DIFF-compatible
# git-difftool--helper script.
#
# This script exports GIT_EXTERNAL_DIFF and GIT_PAGER for use by git.
# GIT_DIFFTOOL_NO_PROMPT, GIT_DIFFTOOL_PROMPT, and GIT_DIFF_TOOL
# are exported for use by git-difftool--helper.
#
# Any arguments that are unknown to this script are forwarded to 'git diff'.

use 5.008;
use strict;
use warnings;
use Cwd qw(abs_path);
use File::Basename qw(dirname);

require Git;

my $DIR = abs_path(dirname($0));


sub usage
{
	print << 'USAGE';
usage: git difftool [-t|--tool=<tool>] [-x|--extcmd=<cmd>]
                    [-y|--no-prompt]   [-g|--gui]
                    ['git diff' options]
USAGE
	exit 1;
}

sub setup_environment
{
	$ENV{PATH} = "$DIR:$ENV{PATH}";
	$ENV{GIT_PAGER} = '';
	$ENV{GIT_EXTERNAL_DIFF} = 'git-difftool--helper';
}

sub exe
{
	my $exe = shift;
	if ($^O eq 'MSWin32' || $^O eq 'msys') {
		return "$exe.exe";
	}
	return $exe;
}

sub generate_command
{
	my @command = (exe('git'), 'diff');
	my $skip_next = 0;
	my $idx = -1;
	my $prompt = '';
	for my $arg (@ARGV) {
		$idx++;
		if ($skip_next) {
			$skip_next = 0;
			next;
		}
		if ($arg eq '-t' || $arg eq '--tool') {
			usage() if $#ARGV <= $idx;
			$ENV{GIT_DIFF_TOOL} = $ARGV[$idx + 1];
			$skip_next = 1;
			next;
		}
		if ($arg =~ /^--tool=/) {
			$ENV{GIT_DIFF_TOOL} = substr($arg, 7);
			next;
		}
		if ($arg eq '-x' || $arg eq '--extcmd') {
			usage() if $#ARGV <= $idx;
			$ENV{GIT_DIFFTOOL_EXTCMD} = $ARGV[$idx + 1];
			$skip_next = 1;
			next;
		}
		if ($arg =~ /^--extcmd=/) {
			$ENV{GIT_DIFFTOOL_EXTCMD} = substr($arg, 9);
			next;
		}
		if ($arg eq '-g' || $arg eq '--gui') {
			eval {
				my $tool = Git::command_oneline('config',
				                                'diff.guitool');
				if (length($tool)) {
					$ENV{GIT_DIFF_TOOL} = $tool;
				}
			};
			next;
		}
		if ($arg eq '-y' || $arg eq '--no-prompt') {
			$prompt = 'no';
			next;
		}
		if ($arg eq '--prompt') {
			$prompt = 'yes';
			next;
		}
		if ($arg eq '-h') {
			usage();
		}
		push @command, $arg;
	}
	if ($prompt eq 'yes') {
		$ENV{GIT_DIFFTOOL_PROMPT} = 'true';
	} elsif ($prompt eq 'no') {
		$ENV{GIT_DIFFTOOL_NO_PROMPT} = 'true';
	}
	return @command
}

setup_environment();

# ActiveState Perl for Win32 does not implement POSIX semantics of
# exec* system call. It just spawns the given executable and finishes
# the starting program, exiting with code 0.
# system will at least catch the errors returned by git diff,
# allowing the caller of git difftool better handling of failures.
my $rc = system(generate_command());
exit($rc | ($rc >> 8));
