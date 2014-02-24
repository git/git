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
use Error qw(:try);
use File::Basename qw(dirname);
use File::Copy;
use File::Find;
use File::stat;
use File::Path qw(mkpath rmtree);
use File::Temp qw(tempdir);
use Getopt::Long qw(:config pass_through);
use Git;

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
	return Git::command_oneline('rev-parse', '--show-toplevel');
}

sub print_tool_help
{
	my $cmd = 'TOOL_MODE=diff';
	$cmd .= ' && . "$(git --exec-path)/git-mergetool--lib"';
	$cmd .= ' && show_tool_help';

	# See the comment at the bottom of file_diff() for the reason behind
	# using system() followed by exit() instead of exec().
	my $rc = system('sh', '-c', $cmd);
	exit($rc | ($rc >> 8));
}

sub exit_cleanup
{
	my ($tmpdir, $status) = @_;
	my $errno = $!;
	rmtree($tmpdir);
	if ($status and $errno) {
		my ($package, $file, $line) = caller();
		warn "$file line $line: $errno\n";
	}
	exit($status | ($status >> 8));
}

sub use_wt_file
{
	my ($repo, $workdir, $file, $sha1) = @_;
	my $null_sha1 = '0' x 40;

	if (! -e "$workdir/$file") {
		# If the file doesn't exist in the working tree, we cannot
		# use it.
		return (0, $null_sha1);
	}

	my $wt_sha1 = $repo->command_oneline('hash-object', "$workdir/$file");
	my $use = ($sha1 eq $null_sha1) || ($sha1 eq $wt_sha1);
	return ($use, $wt_sha1);
}

sub changed_files
{
	my ($repo_path, $index, $worktree) = @_;
	$ENV{GIT_INDEX_FILE} = $index;
	$ENV{GIT_WORK_TREE} = $worktree;
	my $must_unset_git_dir = 0;
	if (not defined($ENV{GIT_DIR})) {
		$must_unset_git_dir = 1;
		$ENV{GIT_DIR} = $repo_path;
	}

	my @refreshargs = qw/update-index --really-refresh -q --unmerged/;
	my @gitargs = qw/diff-files --name-only -z/;
	try {
		Git::command_oneline(@refreshargs);
	} catch Git::Error::Command with {};

	my $line = Git::command_oneline(@gitargs);
	my @files;
	if (defined $line) {
		@files = split('\0', $line);
	} else {
		@files = ();
	}

	delete($ENV{GIT_INDEX_FILE});
	delete($ENV{GIT_WORK_TREE});
	delete($ENV{GIT_DIR}) if ($must_unset_git_dir);

	return map { $_ => 1 } @files;
}

sub setup_dir_diff
{
	my ($repo, $workdir, $symlinks) = @_;

	# Run the diff; exit immediately if no diff found
	# 'Repository' and 'WorkingCopy' must be explicitly set to insure that
	# if $GIT_DIR and $GIT_WORK_TREE are set in ENV, they are actually used
	# by Git->repository->command*.
	my $repo_path = $repo->repo_path();
	my %repo_args = (Repository => $repo_path, WorkingCopy => $workdir);
	my $diffrepo = Git->repository(%repo_args);

	my @gitargs = ('diff', '--raw', '--no-abbrev', '-z', @ARGV);
	my $diffrtn = $diffrepo->command_oneline(@gitargs);
	exit(0) unless defined($diffrtn);

	# Build index info for left and right sides of the diff
	my $submodule_mode = '160000';
	my $symlink_mode = '120000';
	my $null_mode = '0' x 6;
	my $null_sha1 = '0' x 40;
	my $lindex = '';
	my $rindex = '';
	my $wtindex = '';
	my %submodule;
	my %symlink;
	my @working_tree = ();
	my @rawdiff = split('\0', $diffrtn);

	my $i = 0;
	while ($i < $#rawdiff) {
		if ($rawdiff[$i] =~ /^::/) {
			warn << 'EOF';
Combined diff formats ('-c' and '--cc') are not supported in
directory diff mode ('-d' and '--dir-diff').
EOF
			exit(1);
		}

		my ($lmode, $rmode, $lsha1, $rsha1, $status) =
			split(' ', substr($rawdiff[$i], 1));
		my $src_path = $rawdiff[$i + 1];
		my $dst_path;

		if ($status =~ /^[CR]/) {
			$dst_path = $rawdiff[$i + 2];
			$i += 3;
		} else {
			$dst_path = $src_path;
			$i += 2;
		}

		if ($lmode eq $submodule_mode or $rmode eq $submodule_mode) {
			$submodule{$src_path}{left} = $lsha1;
			if ($lsha1 ne $rsha1) {
				$submodule{$dst_path}{right} = $rsha1;
			} else {
				$submodule{$dst_path}{right} = "$rsha1-dirty";
			}
			next;
		}

		if ($lmode eq $symlink_mode) {
			$symlink{$src_path}{left} =
				$diffrepo->command_oneline('show', "$lsha1");
		}

		if ($rmode eq $symlink_mode) {
			$symlink{$dst_path}{right} =
				$diffrepo->command_oneline('show', "$rsha1");
		}

		if ($lmode ne $null_mode and $status !~ /^C/) {
			$lindex .= "$lmode $lsha1\t$src_path\0";
		}

		if ($rmode ne $null_mode) {
			my ($use, $wt_sha1) = use_wt_file($repo, $workdir,
							  $dst_path, $rsha1);
			if ($use) {
				push @working_tree, $dst_path;
				$wtindex .= "$rmode $wt_sha1\t$dst_path\0";
			} else {
				$rindex .= "$rmode $rsha1\t$dst_path\0";
			}
		}
	}

	# Setup temp directories
	my $tmpdir = tempdir('git-difftool.XXXXX', CLEANUP => 0, TMPDIR => 1);
	my $ldir = "$tmpdir/left";
	my $rdir = "$tmpdir/right";
	mkpath($ldir) or exit_cleanup($tmpdir, 1);
	mkpath($rdir) or exit_cleanup($tmpdir, 1);

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
	($inpipe, $ctx) =
		$repo->command_input_pipe(qw(update-index -z --index-info));
	print($inpipe $lindex);
	$repo->command_close_pipe($inpipe, $ctx);

	my $rc = system('git', 'checkout-index', '--all', "--prefix=$ldir/");
	exit_cleanup($tmpdir, $rc) if $rc != 0;

	$ENV{GIT_INDEX_FILE} = "$tmpdir/rindex";
	($inpipe, $ctx) =
		$repo->command_input_pipe(qw(update-index -z --index-info));
	print($inpipe $rindex);
	$repo->command_close_pipe($inpipe, $ctx);

	$rc = system('git', 'checkout-index', '--all', "--prefix=$rdir/");
	exit_cleanup($tmpdir, $rc) if $rc != 0;

	$ENV{GIT_INDEX_FILE} = "$tmpdir/wtindex";
	($inpipe, $ctx) =
		$repo->command_input_pipe(qw(update-index --info-only -z --index-info));
	print($inpipe $wtindex);
	$repo->command_close_pipe($inpipe, $ctx);

	# If $GIT_DIR was explicitly set just for the update/checkout
	# commands, then it should be unset before continuing.
	delete($ENV{GIT_DIR}) if ($must_unset_git_dir);
	delete($ENV{GIT_INDEX_FILE});

	# Changes in the working tree need special treatment since they are
	# not part of the index. Remove any trailing slash from $workdir
	# before starting to avoid double slashes in symlink targets.
	$workdir =~ s|/$||;
	for my $file (@working_tree) {
		my $dir = dirname($file);
		unless (-d "$rdir/$dir") {
			mkpath("$rdir/$dir") or
			exit_cleanup($tmpdir, 1);
		}
		if ($symlinks) {
			symlink("$workdir/$file", "$rdir/$file") or
			exit_cleanup($tmpdir, 1);
		} else {
			copy("$workdir/$file", "$rdir/$file") or
			exit_cleanup($tmpdir, 1);

			my $mode = stat("$workdir/$file")->mode;
			chmod($mode, "$rdir/$file") or
			exit_cleanup($tmpdir, 1);
		}
	}

	# Changes to submodules require special treatment. This loop writes a
	# temporary file to both the left and right directories to show the
	# change in the recorded SHA1 for the submodule.
	for my $path (keys %submodule) {
		my $ok;
		if (defined($submodule{$path}{left})) {
			$ok = write_to_file("$ldir/$path",
				"Subproject commit $submodule{$path}{left}");
		}
		if (defined($submodule{$path}{right})) {
			$ok = write_to_file("$rdir/$path",
				"Subproject commit $submodule{$path}{right}");
		}
		exit_cleanup($tmpdir, 1) if not $ok;
	}

	# Symbolic links require special treatment. The standard "git diff"
	# shows only the link itself, not the contents of the link target.
	# This loop replicates that behavior.
	for my $path (keys %symlink) {
		my $ok;
		if (defined($symlink{$path}{left})) {
			$ok = write_to_file("$ldir/$path",
					$symlink{$path}{left});
		}
		if (defined($symlink{$path}{right})) {
			$ok = write_to_file("$rdir/$path",
					$symlink{$path}{right});
		}
		exit_cleanup($tmpdir, 1) if not $ok;
	}

	return ($ldir, $rdir, $tmpdir, @working_tree);
}

sub write_to_file
{
	my $path = shift;
	my $value = shift;

	# Make sure the path to the file exists
	my $dir = dirname($path);
	unless (-d "$dir") {
		mkpath("$dir") or return 0;
	}

	# If the file already exists in that location, delete it.  This
	# is required in the case of symbolic links.
	unlink($path);

	open(my $fh, '>', $path) or return 0;
	print($fh $value);
	close($fh);

	return 1;
}

sub main
{
	# parse command-line options. all unrecognized options and arguments
	# are passed through to the 'git diff' command.
	my %opts = (
		difftool_cmd => undef,
		dirdiff => undef,
		extcmd => undef,
		gui => undef,
		help => undef,
		prompt => undef,
		symlinks => $^O ne 'cygwin' &&
				$^O ne 'MSWin32' && $^O ne 'msys',
		tool_help => undef,
	);
	GetOptions('g|gui!' => \$opts{gui},
		'd|dir-diff' => \$opts{dirdiff},
		'h' => \$opts{help},
		'prompt!' => \$opts{prompt},
		'y' => sub { $opts{prompt} = 0; },
		'symlinks' => \$opts{symlinks},
		'no-symlinks' => sub { $opts{symlinks} = 0; },
		't|tool:s' => \$opts{difftool_cmd},
		'tool-help' => \$opts{tool_help},
		'x|extcmd:s' => \$opts{extcmd});

	if (defined($opts{help})) {
		usage(0);
	}
	if (defined($opts{tool_help})) {
		print_tool_help();
	}
	if (defined($opts{difftool_cmd})) {
		if (length($opts{difftool_cmd}) > 0) {
			$ENV{GIT_DIFF_TOOL} = $opts{difftool_cmd};
		} else {
			print "No <tool> given for --tool=<tool>\n";
			usage(1);
		}
	}
	if (defined($opts{extcmd})) {
		if (length($opts{extcmd}) > 0) {
			$ENV{GIT_DIFFTOOL_EXTCMD} = $opts{extcmd};
		} else {
			print "No <cmd> given for --extcmd=<cmd>\n";
			usage(1);
		}
	}
	if ($opts{gui}) {
		my $guitool = Git::config('diff.guitool');
		if (defined($guitool) && length($guitool) > 0) {
			$ENV{GIT_DIFF_TOOL} = $guitool;
		}
	}

	# In directory diff mode, 'git-difftool--helper' is called once
	# to compare the a/b directories.  In file diff mode, 'git diff'
	# will invoke a separate instance of 'git-difftool--helper' for
	# each file that changed.
	if (defined($opts{dirdiff})) {
		dir_diff($opts{extcmd}, $opts{symlinks});
	} else {
		file_diff($opts{prompt});
	}
}

sub dir_diff
{
	my ($extcmd, $symlinks) = @_;
	my $rc;
	my $error = 0;
	my $repo = Git->repository();
	my $workdir = find_worktree();
	my ($a, $b, $tmpdir, @worktree) =
		setup_dir_diff($repo, $workdir, $symlinks);

	if (defined($extcmd)) {
		$rc = system($extcmd, $a, $b);
	} else {
		$ENV{GIT_DIFFTOOL_DIRDIFF} = 'true';
		$rc = system('git', 'difftool--helper', $a, $b);
	}
	# If the diff including working copy files and those
	# files were modified during the diff, then the changes
	# should be copied back to the working tree.
	# Do not copy back files when symlinks are used and the
	# external tool did not replace the original link with a file.
	#
	# These hashes are loaded lazily since they aren't needed
	# in the common case of --symlinks and the difftool updating
	# files through the symlink.
	my %wt_modified;
	my %tmp_modified;
	my $indices_loaded = 0;

	for my $file (@worktree) {
		next if $symlinks && -l "$b/$file";
		next if ! -f "$b/$file";

		if (!$indices_loaded) {
			%wt_modified = changed_files($repo->repo_path(),
				"$tmpdir/wtindex", "$workdir");
			%tmp_modified = changed_files($repo->repo_path(),
				"$tmpdir/wtindex", "$b");
			$indices_loaded = 1;
		}

		if (exists $wt_modified{$file} and exists $tmp_modified{$file}) {
			my $errmsg = "warning: Both files modified: ";
			$errmsg .= "'$workdir/$file' and '$b/$file'.\n";
			$errmsg .= "warning: Working tree file has been left.\n";
			$errmsg .= "warning:\n";
			warn $errmsg;
			$error = 1;
		} elsif (exists $tmp_modified{$file}) {
			my $mode = stat("$b/$file")->mode;
			copy("$b/$file", "$workdir/$file") or
			exit_cleanup($tmpdir, 1);

			chmod($mode, "$workdir/$file") or
			exit_cleanup($tmpdir, 1);
		}
	}
	if ($error) {
		warn "warning: Temporary files exist in '$tmpdir'.\n";
		warn "warning: You may want to cleanup or recover these.\n";
		exit(1);
	} else {
		exit_cleanup($tmpdir, $rc);
	}
}

sub file_diff
{
	my ($prompt) = @_;

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

main();
