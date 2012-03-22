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
use Getopt::Long qw(:config pass_through);
use Git;

sub usage
{
	my $exitcode = shift;
	print << 'USAGE';
usage: git difftool [-t|--tool=<tool>]
                    [-x|--extcmd=<cmd>]
                    [-g|--gui] [--no-gui]
                    [--prompt] [-y|--no-prompt]
                    ['git diff' options]
USAGE
	exit($exitcode);
}

sub setup_environment
{
	my $DIR = abs_path(dirname($0));
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

# parse command-line options. all unrecognized options and arguments
# are passed through to the 'git diff' command.
my ($difftool_cmd, $extcmd, $gui, $help, $prompt);
GetOptions('g|gui!' => \$gui,
	'h' => \$help,
	'prompt!' => \$prompt,
	'y' => sub { $prompt = 0; },
	't|tool:s' => \$difftool_cmd,
	'x|extcmd:s' => \$extcmd);

if (defined($help)) {
	usage(0);
}
if (defined($difftool_cmd)) {
	if (length($difftool_cmd) > 0) {
		$ENV{GIT_DIFF_TOOL} = $difftool_cmd;
	} else {
		print "No <tool> given for --tool=<tool>\n";
		usage(1);
	}
}
if (defined($extcmd)) {
	if (length($extcmd) > 0) {
		$ENV{GIT_DIFFTOOL_EXTCMD} = $extcmd;
	} else {
		print "No <cmd> given for --extcmd=<cmd>\n";
		usage(1);
	}
}
if ($gui) {
	my $guitool = "";
	$guitool = Git::config('diff.guitool');
	if (length($guitool) > 0) {
		$ENV{GIT_DIFF_TOOL} = $guitool;
	}
}
if (defined($prompt)) {
	if ($prompt) {
		$ENV{GIT_DIFFTOOL_PROMPT} = 'true';
	} else {
		$ENV{GIT_DIFFTOOL_NO_PROMPT} = 'true';
	}
}

setup_environment();
my @command = (exe('git'), 'diff', @ARGV);

# ActiveState Perl for Win32 does not implement POSIX semantics of
# exec* system call. It just spawns the given executable and finishes
# the starting program, exiting with code 0.
# system will at least catch the errors returned by git diff,
# allowing the caller of git difftool better handling of failures.
my $rc = system(@command);
exit($rc | ($rc >> 8));
