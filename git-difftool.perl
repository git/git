#!/usr/bin/perl
# Copyright (c) 2009, 2010 David Aguilar
# Copyright (c) 2012 Tim Henigan
#
# This is a wrapper around the GIT_EXTERNAL_DIFF-compatible
# git-difftool--helper script.
#
# This script exports GIT_EXTERNAL_DIFF and GIT_PAGER for use by git.
# The GIT_DIFF* variables are exported for use by git-difftool--helper.
#
# Any arguments that are unknown to this script are forwarded to 'git diff'.

use 5.008;
use strict;
use warnings;
use File::Basename qw(dirname);
use File::Copy;
use File::Find;
use File::stat;
use File::Path qw(mkpath);
use File::Temp qw(tempdir);
use Getopt::Long qw(:config pass_through);
use Git;

my @tools;
my @working_tree;
my $rc;
my $repo = Git->repository();
my $repo_path = $repo->repo_path();

sub usage
{
	my $exitcode = shift;
	print << 'USAGE';
usage: git difftool [-t|--tool=<tool>] [--tool-help]
                    [-x|--extcmd=<cmd>]
                    [-g|--gui] [--no-gui]
                    [--prompt] [-y|--no-prompt]
                    [-d|--dir-diff]
                    ['git diff' options]
USAGE
	exit($exitcode);
}

sub find_worktree
{
	# Git->repository->wc_path() does not honor changes to the working
	# tree location made by $ENV{GIT_WORK_TREE} or the 'core.worktree'
	# config variable.
	my $worktree;
	my $env_worktree = $ENV{GIT_WORK_TREE};
	my $core_worktree = Git::config('core.worktree');

	if (defined($env_worktree) and (length($env_worktree) > 0)) {
		$worktree = $env_worktree;
	} elsif (defined($core_worktree) and (length($core_worktree) > 0)) {
		$worktree = $core_worktree;
	} else {
		$worktree = $repo->wc_path();
	}

	return $worktree;
}

my $workdir = find_worktree();

sub filter_tool_scripts
{
	if (-d $_) {
		if ($_ ne ".") {
			# Ignore files in subdirectories
			$File::Find::prune = 1;
		}
	} else {
		if ((-f $_) && ($_ ne "defaults")) {
			push(@tools, $_);
		}
	}
}

sub print_tool_help
{
	my ($cmd, @found, @notfound);
	my $gitpath = Git::exec_path();

	find(\&filter_tool_scripts, "$gitpath/mergetools");

	foreach my $tool (@tools) {
		$cmd  = "TOOL_MODE=diff";
		$cmd .= ' && . "$(git --exec-path)/git-mergetool--lib"';
		$cmd .= " && get_merge_tool_path $tool >/dev/null 2>&1";
		$cmd .= " && can_diff >/dev/null 2>&1";
		if (system('sh', '-c', $cmd) == 0) {
			push(@found, $tool);
		} else {
			push(@notfound, $tool);
		}
	}

	print "'git difftool --tool=<tool>' may be set to one of the following:\n";
	print "\t$_\n" for (sort(@found));

	print "\nThe following tools are valid, but not currently available:\n";
	print "\t$_\n" for (sort(@notfound));

	print "\nNOTE: Some of the tools listed above only work in a windowed\n";
	print "environment. If run in a terminal-only session, they will fail.\n";

	exit(0);
}

sub setup_dir_diff
{
	# Run the diff; exit immediately if no diff found
	# 'Repository' and 'WorkingCopy' must be explicitly set to insure that
	# if $GIT_DIR and $GIT_WORK_TREE are set in ENV, they are actually used
	# by Git->repository->command*.
	my $diffrepo = Git->repository(Repository => $repo_path, WorkingCopy => $workdir);
	my $diffrtn = $diffrepo->command_oneline('diff', '--raw', '--no-abbrev', '-z', @ARGV);
	exit(0) if (length($diffrtn) == 0);

	# Setup temp directories
	my $tmpdir = tempdir('git-diffall.XXXXX', CLEANUP => 1, TMPDIR => 1);
	my $ldir = "$tmpdir/left";
	my $rdir = "$tmpdir/right";
	mkpath($ldir) or die $!;
	mkpath($rdir) or die $!;

	# Build index info for left and right sides of the diff
	my $submodule_mode = '160000';
	my $symlink_mode = '120000';
	my $null_mode = '0' x 6;
	my $null_sha1 = '0' x 40;
	my $lindex = '';
	my $rindex = '';
	my %submodule;
	my %symlink;
	my @rawdiff = split('\0', $diffrtn);

	my $i = 0;
	while ($i < $#rawdiff) {
		if ($rawdiff[$i] =~ /^::/) {
			print "Combined diff formats ('-c' and '--cc') are not supported in directory diff mode.\n";
			exit(1);
		}

		my ($lmode, $rmode, $lsha1, $rsha1, $status) = split(' ', substr($rawdiff[$i], 1));
		my $src_path = $rawdiff[$i + 1];
		my $dst_path;

		if ($status =~ /^[CR]/) {
			$dst_path = $rawdiff[$i + 2];
			$i += 3;
		} else {
			$dst_path = $src_path;
			$i += 2;
		}

		if (($lmode eq $submodule_mode) or ($rmode eq $submodule_mode)) {
			$submodule{$src_path}{left} = $lsha1;
			if ($lsha1 ne $rsha1) {
				$submodule{$dst_path}{right} = $rsha1;
			} else {
				$submodule{$dst_path}{right} = "$rsha1-dirty";
			}
			next;
		}

		if ($lmode eq $symlink_mode) {
			$symlink{$src_path}{left} = $diffrepo->command_oneline('show', "$lsha1");
		}

		if ($rmode eq $symlink_mode) {
			$symlink{$dst_path}{right} = $diffrepo->command_oneline('show', "$rsha1");
		}

		if (($lmode ne $null_mode) and ($status !~ /^C/)) {
			$lindex .= "$lmode $lsha1\t$src_path\0";
		}

		if ($rmode ne $null_mode) {
			if ($rsha1 ne $null_sha1) {
				$rindex .= "$rmode $rsha1\t$dst_path\0";
			} else {
				push(@working_tree, $dst_path);
			}
		}
	}

	# If $GIT_DIR is not set prior to calling 'git update-index' and
	# 'git checkout-index', then those commands will fail if difftool
	# is called from a directory other than the repo root.
	my $must_unset_git_dir = 0;
	if (not defined($ENV{GIT_DIR})) {
		$must_unset_git_dir = 1;
		$ENV{GIT_DIR} = $repo_path;
	}

	# Populate the left and right directories based on each index file
	my ($inpipe, $ctx);
	$ENV{GIT_INDEX_FILE} = "$tmpdir/lindex";
	($inpipe, $ctx) = $repo->command_input_pipe(qw/update-index -z --index-info/);
	print($inpipe $lindex);
	$repo->command_close_pipe($inpipe, $ctx);
	$rc = system('git', 'checkout-index', '--all', "--prefix=$ldir/");
	exit($rc | ($rc >> 8)) if ($rc != 0);

	$ENV{GIT_INDEX_FILE} = "$tmpdir/rindex";
	($inpipe, $ctx) = $repo->command_input_pipe(qw/update-index -z --index-info/);
	print($inpipe $rindex);
	$repo->command_close_pipe($inpipe, $ctx);
	$rc = system('git', 'checkout-index', '--all', "--prefix=$rdir/");
	exit($rc | ($rc >> 8)) if ($rc != 0);

	# If $GIT_DIR was explicitly set just for the update/checkout
	# commands, then it should be unset before continuing.
	delete($ENV{GIT_DIR}) if ($must_unset_git_dir);
	delete($ENV{GIT_INDEX_FILE});

	# Changes in the working tree need special treatment since they are
	# not part of the index
	for my $file (@working_tree) {
		my $dir = dirname($file);
		unless (-d "$rdir/$dir") {
			mkpath("$rdir/$dir") or die $!;
		}
		copy("$workdir/$file", "$rdir/$file") or die $!;
		chmod(stat("$workdir/$file")->mode, "$rdir/$file") or die $!;
	}

	# Changes to submodules require special treatment. This loop writes a
	# temporary file to both the left and right directories to show the
	# change in the recorded SHA1 for the submodule.
	for my $path (keys %submodule) {
		if (defined($submodule{$path}{left})) {
			write_to_file("$ldir/$path", "Subproject commit $submodule{$path}{left}");
		}
		if (defined($submodule{$path}{right})) {
			write_to_file("$rdir/$path", "Subproject commit $submodule{$path}{right}");
		}
	}

	# Symbolic links require special treatment. The standard "git diff"
	# shows only the link itself, not the contents of the link target.
	# This loop replicates that behavior.
	for my $path (keys %symlink) {
		if (defined($symlink{$path}{left})) {
			write_to_file("$ldir/$path", $symlink{$path}{left});
		}
		if (defined($symlink{$path}{right})) {
			write_to_file("$rdir/$path", $symlink{$path}{right});
		}
	}

	return ($ldir, $rdir);
}

sub write_to_file
{
	my $path = shift;
	my $value = shift;

	# Make sure the path to the file exists
	my $dir = dirname($path);
	unless (-d "$dir") {
		mkpath("$dir") or die $!;
	}

	# If the file already exists in that location, delete it.  This
	# is required in the case of symbolic links.
	unlink("$path");

	open(my $fh, '>', "$path") or die $!;
	print($fh $value);
	close($fh);
}

# parse command-line options. all unrecognized options and arguments
# are passed through to the 'git diff' command.
my ($difftool_cmd, $dirdiff, $extcmd, $gui, $help, $prompt, $tool_help);
GetOptions('g|gui!' => \$gui,
	'd|dir-diff' => \$dirdiff,
	'h' => \$help,
	'prompt!' => \$prompt,
	'y' => sub { $prompt = 0; },
	't|tool:s' => \$difftool_cmd,
	'tool-help' => \$tool_help,
	'x|extcmd:s' => \$extcmd);

if (defined($help)) {
	usage(0);
}
if (defined($tool_help)) {
	print_tool_help();
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
	my $guitool = '';
	$guitool = Git::config('diff.guitool');
	if (length($guitool) > 0) {
		$ENV{GIT_DIFF_TOOL} = $guitool;
	}
}

# In directory diff mode, 'git-difftool--helper' is called once
# to compare the a/b directories.  In file diff mode, 'git diff'
# will invoke a separate instance of 'git-difftool--helper' for
# each file that changed.
if (defined($dirdiff)) {
	my ($a, $b) = setup_dir_diff();
	if (defined($extcmd)) {
		$rc = system($extcmd, $a, $b);
	} else {
		$ENV{GIT_DIFFTOOL_DIRDIFF} = 'true';
		$rc = system('git', 'difftool--helper', $a, $b);
	}

	exit($rc | ($rc >> 8)) if ($rc != 0);

	# If the diff including working copy files and those
	# files were modified during the diff, then the changes
	# should be copied back to the working tree
	for my $file (@working_tree) {
		copy("$b/$file", "$workdir/$file") or die $!;
		chmod(stat("$b/$file")->mode, "$workdir/$file") or die $!;
	}
} else {
	if (defined($prompt)) {
		if ($prompt) {
			$ENV{GIT_DIFFTOOL_PROMPT} = 'true';
		} else {
			$ENV{GIT_DIFFTOOL_NO_PROMPT} = 'true';
		}
	}

	$ENV{GIT_PAGER} = '';
	$ENV{GIT_EXTERNAL_DIFF} = 'git-difftool--helper';

	# ActiveState Perl for Win32 does not implement POSIX semantics of
	# exec* system call. It just spawns the given executable and finishes
	# the starting program, exiting with code 0.
	# system will at least catch the errors returned by git diff,
	# allowing the caller of git difftool better handling of failures.
	my $rc = system('git', 'diff', @ARGV);
	exit($rc | ($rc >> 8));
}
