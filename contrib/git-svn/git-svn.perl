#!/usr/bin/env perl
# Copyright (C) 2006, Eric Wong <normalperson@yhbt.net>
# License: GPL v2 or later
use warnings;
use strict;
use vars qw/	$AUTHOR $VERSION
		$SVN_URL $SVN_INFO $SVN_WC $SVN_UUID
		$GIT_SVN_INDEX $GIT_SVN
		$GIT_DIR $GIT_SVN_DIR $REVDB/;
$AUTHOR = 'Eric Wong <normalperson@yhbt.net>';
$VERSION = '1.1.1-broken';

use Cwd qw/abs_path/;
$GIT_DIR = abs_path($ENV{GIT_DIR} || '.git');
$ENV{GIT_DIR} = $GIT_DIR;

my $LC_ALL = $ENV{LC_ALL};
my $TZ = $ENV{TZ};
# make sure the svn binary gives consistent output between locales and TZs:
$ENV{TZ} = 'UTC';
$ENV{LC_ALL} = 'C';

# If SVN:: library support is added, please make the dependencies
# optional and preserve the capability to use the command-line client.
# use eval { require SVN::... } to make it lazy load
# We don't use any modules not in the standard Perl distribution:
use Carp qw/croak/;
use IO::File qw//;
use File::Basename qw/dirname basename/;
use File::Path qw/mkpath/;
use Getopt::Long qw/:config gnu_getopt no_ignore_case auto_abbrev pass_through/;
use File::Spec qw//;
use POSIX qw/strftime/;
use Memoize;
memoize('revisions_eq');

my ($SVN_PATH, $SVN, $SVN_LOG, $_use_lib);
$_use_lib = 1 unless $ENV{GIT_SVN_NO_LIB};
libsvn_load();
my $_optimize_commits = 1 unless $ENV{GIT_SVN_NO_OPTIMIZE_COMMITS};
my $sha1 = qr/[a-f\d]{40}/;
my $sha1_short = qr/[a-f\d]{4,40}/;
my ($_revision,$_stdin,$_no_ignore_ext,$_no_stop_copy,$_help,$_rmdir,$_edit,
	$_find_copies_harder, $_l, $_cp_similarity,
	$_repack, $_repack_nr, $_repack_flags,
	$_template, $_shared, $_no_default_regex, $_no_graft_copy,
	$_limit, $_verbose, $_incremental, $_oneline, $_l_fmt, $_show_commit,
	$_version, $_upgrade, $_authors, $_branch_all_refs, @_opt_m);
my (@_branch_from, %tree_map, %users, %rusers, %equiv);
my ($_svn_co_url_revs, $_svn_pg_peg_revs);
my @repo_path_split_cache;

my %fc_opts = ( 'no-ignore-externals' => \$_no_ignore_ext,
		'branch|b=s' => \@_branch_from,
		'branch-all-refs|B' => \$_branch_all_refs,
		'authors-file|A=s' => \$_authors,
		'repack:i' => \$_repack,
		'repack-flags|repack-args|repack-opts=s' => \$_repack_flags);

my ($_trunk, $_tags, $_branches);
my %multi_opts = ( 'trunk|T=s' => \$_trunk,
		'tags|t=s' => \$_tags,
		'branches|b=s' => \$_branches );
my %init_opts = ( 'template=s' => \$_template, 'shared' => \$_shared );

# yes, 'native' sets "\n".  Patches to fix this for non-*nix systems welcome:
my %EOL = ( CR => "\015", LF => "\012", CRLF => "\015\012", native => "\012" );

my %cmd = (
	fetch => [ \&fetch, "Download new revisions from SVN",
			{ 'revision|r=s' => \$_revision, %fc_opts } ],
	init => [ \&init, "Initialize a repo for tracking" .
			  " (requires URL argument)",
			  \%init_opts ],
	commit => [ \&commit, "Commit git revisions to SVN",
			{	'stdin|' => \$_stdin,
				'edit|e' => \$_edit,
				'rmdir' => \$_rmdir,
				'find-copies-harder' => \$_find_copies_harder,
				'l=i' => \$_l,
				'copy-similarity|C=i'=> \$_cp_similarity,
				%fc_opts,
			} ],
	'show-ignore' => [ \&show_ignore, "Show svn:ignore listings",
			{ 'revision|r=i' => \$_revision } ],
	rebuild => [ \&rebuild, "Rebuild git-svn metadata (after git clone)",
			{ 'no-ignore-externals' => \$_no_ignore_ext,
			  'upgrade' => \$_upgrade } ],
	'graft-branches' => [ \&graft_branches,
			'Detect merges/branches from already imported history',
			{ 'merge-rx|m' => \@_opt_m,
			  'no-default-regex' => \$_no_default_regex,
			  'no-graft-copy' => \$_no_graft_copy } ],
	'multi-init' => [ \&multi_init,
			'Initialize multiple trees (like git-svnimport)',
			{ %multi_opts, %fc_opts } ],
	'multi-fetch' => [ \&multi_fetch,
			'Fetch multiple trees (like git-svnimport)',
			\%fc_opts ],
	'log' => [ \&show_log, 'Show commit logs',
			{ 'limit=i' => \$_limit,
			  'revision|r=s' => \$_revision,
			  'verbose|v' => \$_verbose,
			  'incremental' => \$_incremental,
			  'oneline' => \$_oneline,
			  'show-commit' => \$_show_commit,
			  'authors-file|A=s' => \$_authors,
			} ],
);

my $cmd;
for (my $i = 0; $i < @ARGV; $i++) {
	if (defined $cmd{$ARGV[$i]}) {
		$cmd = $ARGV[$i];
		splice @ARGV, $i, 1;
		last;
	}
};

my %opts = %{$cmd{$cmd}->[2]} if (defined $cmd);

read_repo_config(\%opts);
my $rv = GetOptions(%opts, 'help|H|h' => \$_help,
				'version|V' => \$_version,
				'id|i=s' => \$GIT_SVN);
exit 1 if (!$rv && $cmd ne 'log');

set_default_vals();
usage(0) if $_help;
version() if $_version;
usage(1) unless defined $cmd;
init_vars();
load_authors() if $_authors;
load_all_refs() if $_branch_all_refs;
svn_compat_check();
migration_check() unless $cmd =~ /^(?:init|multi-init)$/;
$cmd{$cmd}->[0]->(@ARGV);
exit 0;

####################### primary functions ######################
sub usage {
	my $exit = shift || 0;
	my $fd = $exit ? \*STDERR : \*STDOUT;
	print $fd <<"";
git-svn - bidirectional operations between a single Subversion tree and git
Usage: $0 <command> [options] [arguments]\n

	print $fd "Available commands:\n" unless $cmd;

	foreach (sort keys %cmd) {
		next if $cmd && $cmd ne $_;
		print $fd '  ',pack('A13',$_),$cmd{$_}->[1],"\n";
		foreach (keys %{$cmd{$_}->[2]}) {
			# prints out arguments as they should be passed:
			my $x = s#[:=]s$## ? '<arg>' : s#[:=]i$## ? '<num>' : '';
			print $fd ' ' x 17, join(', ', map { length $_ > 1 ?
							"--$_" : "-$_" }
						split /\|/,$_)," $x\n";
		}
	}
	print $fd <<"";
\nGIT_SVN_ID may be set in the environment or via the --id/-i switch to an
arbitrary identifier if you're tracking multiple SVN branches/repositories in
one git repository and want to keep them separate.  See git-svn(1) for more
information.

	exit $exit;
}

sub version {
	print "git-svn version $VERSION\n";
	exit 0;
}

sub rebuild {
	$SVN_URL = shift or undef;
	my $newest_rev = 0;
	if ($_upgrade) {
		sys('git-update-ref',"refs/remotes/$GIT_SVN","$GIT_SVN-HEAD");
	} else {
		check_upgrade_needed();
	}

	my $pid = open(my $rev_list,'-|');
	defined $pid or croak $!;
	if ($pid == 0) {
		exec("git-rev-list","refs/remotes/$GIT_SVN") or croak $!;
	}
	my $latest;
	while (<$rev_list>) {
		chomp;
		my $c = $_;
		croak "Non-SHA1: $c\n" unless $c =~ /^$sha1$/o;
		my @commit = grep(/^git-svn-id: /,`git-cat-file commit $c`);
		next if (!@commit); # skip merges
		my ($url, $rev, $uuid) = extract_metadata($commit[$#commit]);
		if (!$rev || !$uuid) {
			croak "Unable to extract revision or UUID from ",
				"$c, $commit[$#commit]\n";
		}

		# if we merged or otherwise started elsewhere, this is
		# how we break out of it
		next if (defined $SVN_UUID && ($uuid ne $SVN_UUID));
		next if (defined $SVN_URL && defined $url && ($url ne $SVN_URL));

		unless (defined $latest) {
			if (!$SVN_URL && !$url) {
				croak "SVN repository location required: $url\n";
			}
			$SVN_URL ||= $url;
			$SVN_UUID ||= $uuid;
			setup_git_svn();
			$latest = $rev;
		}
		revdb_set($REVDB, $rev, $c);
		print "r$rev = $c\n";
		$newest_rev = $rev if ($rev > $newest_rev);
	}
	close $rev_list or croak $?;

	goto out if $_use_lib;
	if (!chdir $SVN_WC) {
		svn_cmd_checkout($SVN_URL, $latest, $SVN_WC);
		chdir $SVN_WC or croak $!;
	}

	$pid = fork;
	defined $pid or croak $!;
	if ($pid == 0) {
		my @svn_up = qw(svn up);
		push @svn_up, '--ignore-externals' unless $_no_ignore_ext;
		sys(@svn_up,"-r$newest_rev");
		$ENV{GIT_INDEX_FILE} = $GIT_SVN_INDEX;
		index_changes();
		exec('git-write-tree') or croak $!;
	}
	waitpid $pid, 0;
	croak $? if $?;
out:
	if ($_upgrade) {
		print STDERR <<"";
Keeping deprecated refs/head/$GIT_SVN-HEAD for now.  Please remove it
when you have upgraded your tools and habits to use refs/remotes/$GIT_SVN

	}
}

sub init {
	$SVN_URL = shift or die "SVN repository location required " .
				"as a command-line argument\n";
	$SVN_URL =~ s!/+$!!; # strip trailing slash
	unless (-d $GIT_DIR) {
		my @init_db = ('git-init-db');
		push @init_db, "--template=$_template" if defined $_template;
		push @init_db, "--shared" if defined $_shared;
		sys(@init_db);
	}
	setup_git_svn();
}

sub fetch {
	check_upgrade_needed();
	$SVN_URL ||= file_to_s("$GIT_SVN_DIR/info/url");
	my $ret = $_use_lib ? fetch_lib(@_) : fetch_cmd(@_);
	if ($ret->{commit} && quiet_run(qw(git-rev-parse --verify
						refs/heads/master^0))) {
		sys(qw(git-update-ref refs/heads/master),$ret->{commit});
	}
	return $ret;
}

sub fetch_cmd {
	my (@parents) = @_;
	my @log_args = -d $SVN_WC ? ($SVN_WC) : ($SVN_URL);
	unless ($_revision) {
		$_revision = -d $SVN_WC ? 'BASE:HEAD' : '0:HEAD';
	}
	push @log_args, "-r$_revision";
	push @log_args, '--stop-on-copy' unless $_no_stop_copy;

	my $svn_log = svn_log_raw(@log_args);

	my $base = next_log_entry($svn_log) or croak "No base revision!\n";
	# don't need last_revision from grab_base_rev() because
	# user could've specified a different revision to skip (they
	# didn't want to import certain revisions into git for whatever
	# reason, so trust $base->{revision} instead.
	my (undef, $last_commit) = svn_grab_base_rev();
	unless (-d $SVN_WC) {
		svn_cmd_checkout($SVN_URL,$base->{revision},$SVN_WC);
		chdir $SVN_WC or croak $!;
		read_uuid();
		$last_commit = git_commit($base, @parents);
		assert_tree($last_commit);
	} else {
		chdir $SVN_WC or croak $!;
		read_uuid();
		# looks like a user manually cp'd and svn switch'ed
		unless ($last_commit) {
			sys(qw/svn revert -R ./);
			assert_svn_wc_clean($base->{revision});
			$last_commit = git_commit($base, @parents);
			assert_tree($last_commit);
		}
	}
	my @svn_up = qw(svn up);
	push @svn_up, '--ignore-externals' unless $_no_ignore_ext;
	my $last = $base;
	while (my $log_msg = next_log_entry($svn_log)) {
		if ($last->{revision} >= $log_msg->{revision}) {
			croak "Out of order: last >= current: ",
				"$last->{revision} >= $log_msg->{revision}\n";
		}
		# Revert is needed for cases like:
		# https://svn.musicpd.org/Jamming/trunk (r166:167), but
		# I can't seem to reproduce something like that on a test...
		sys(qw/svn revert -R ./);
		assert_svn_wc_clean($last->{revision});
		sys(@svn_up,"-r$log_msg->{revision}");
		$last_commit = git_commit($log_msg, $last_commit, @parents);
		$last = $log_msg;
	}
	close $svn_log->{fh};
	$last->{commit} = $last_commit;
	return $last;
}

sub fetch_lib {
	my (@parents) = @_;
	$SVN_URL ||= file_to_s("$GIT_SVN_DIR/info/url");
	my $repo;
	($repo, $SVN_PATH) = repo_path_split($SVN_URL);
	$SVN_LOG ||= libsvn_connect($repo);
	$SVN ||= libsvn_connect($repo);
	my ($last_rev, $last_commit) = svn_grab_base_rev();
	my ($base, $head) = libsvn_parse_revision($last_rev);
	if ($base > $head) {
		return { revision => $last_rev, commit => $last_commit }
	}
	my $index = set_index($GIT_SVN_INDEX);

	# limit ourselves and also fork() since get_log won't release memory
	# after processing a revision and SVN stuff seems to leak
	my $inc = 1000;
	my ($min, $max) = ($base, $head < $base+$inc ? $head : $base+$inc);
	read_uuid();
	if (defined $last_commit) {
		unless (-e $GIT_SVN_INDEX) {
			sys(qw/git-read-tree/, $last_commit);
		}
		chomp (my $x = `git-write-tree`);
		my ($y) = (`git-cat-file commit $last_commit`
							=~ /^tree ($sha1)/m);
		if ($y ne $x) {
			unlink $GIT_SVN_INDEX or croak $!;
			sys(qw/git-read-tree/, $last_commit);
		}
		chomp ($x = `git-write-tree`);
		if ($y ne $x) {
			print STDERR "trees ($last_commit) $y != $x\n",
				 "Something is seriously wrong...\n";
		}
	}
	while (1) {
		# fork, because using SVN::Pool with get_log() still doesn't
		# seem to help enough to keep memory usage down.
		defined(my $pid = fork) or croak $!;
		if (!$pid) {
			$SVN::Error::handler = \&libsvn_skip_unknown_revs;
			print "Fetching revisions $min .. $max\n";

			# Yes I'm perfectly aware that the fourth argument
			# below is the limit revisions number.  Unfortunately
			# performance sucks with it enabled, so it's much
			# faster to fetch revision ranges instead of relying
			# on the limiter.
			$SVN_LOG->get_log( '/'.$SVN_PATH, $min, $max, 0, 1, 1,
				sub {
					my $log_msg;
					if ($last_commit) {
						$log_msg = libsvn_fetch(
							$last_commit, @_);
						$last_commit = git_commit(
							$log_msg,
							$last_commit,
							@parents);
					} else {
						$log_msg = libsvn_new_tree(@_);
						$last_commit = git_commit(
							$log_msg, @parents);
					}
				});
			$SVN::Error::handler = sub { 'quiet warnings' };
			exit 0;
		}
		waitpid $pid, 0;
		croak $? if $?;
		($last_rev, $last_commit) = svn_grab_base_rev();
		last if ($max >= $head);
		$min = $max + 1;
		$max += $inc;
		$max = $head if ($max > $head);
	}
	restore_index($index);
	return { revision => $last_rev, commit => $last_commit };
}

sub commit {
	my (@commits) = @_;
	check_upgrade_needed();
	if ($_stdin || !@commits) {
		print "Reading from stdin...\n";
		@commits = ();
		while (<STDIN>) {
			if (/\b($sha1_short)\b/o) {
				unshift @commits, $1;
			}
		}
	}
	my @revs;
	foreach my $c (@commits) {
		chomp(my @tmp = safe_qx('git-rev-parse',$c));
		if (scalar @tmp == 1) {
			push @revs, $tmp[0];
		} elsif (scalar @tmp > 1) {
			push @revs, reverse (safe_qx('git-rev-list',@tmp));
		} else {
			die "Failed to rev-parse $c\n";
		}
	}
	chomp @revs;
	$_use_lib ? commit_lib(@revs) : commit_cmd(@revs);
	print "Done committing ",scalar @revs," revisions to SVN\n";
}

sub commit_cmd {
	my (@revs) = @_;

	chdir $SVN_WC or croak "Unable to chdir $SVN_WC: $!\n";
	my $info = svn_info('.');
	my $fetched = fetch();
	if ($info->{Revision} != $fetched->{revision}) {
		print STDERR "There are new revisions that were fetched ",
				"and need to be merged (or acknowledged) ",
				"before committing.\n";
		exit 1;
	}
	$info = svn_info('.');
	read_uuid($info);
	my $last = $fetched;
	foreach my $c (@revs) {
		my $mods = svn_checkout_tree($last, $c);
		if (scalar @$mods == 0) {
			print "Skipping, no changes detected\n";
			next;
		}
		$last = svn_commit_tree($last, $c);
	}
}

sub commit_lib {
	my (@revs) = @_;
	my ($r_last, $cmt_last) = svn_grab_base_rev();
	defined $r_last or die "Must have an existing revision to commit\n";
	my $fetched = fetch_lib();
	if ($r_last != $fetched->{revision}) {
		print STDERR "There are new revisions that were fetched ",
				"and need to be merged (or acknowledged) ",
				"before committing.\n",
				"last rev: $r_last\n",
				" current: $fetched->{revision}\n";
		exit 1;
	}
	read_uuid();
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef, 0) : ();
	my $commit_msg = "$GIT_SVN_DIR/.svn-commit.tmp.$$";

	foreach my $c (@revs) {
		# fork for each commit because there's a memory leak I
		# can't track down... (it's probably in the SVN code)
		defined(my $pid = open my $fh, '-|') or croak $!;
		if (!$pid) {
			if (defined $LC_ALL) {
				$ENV{LC_ALL} = $LC_ALL;
			} else {
				delete $ENV{LC_ALL};
			}
			my $log_msg = get_commit_message($c, $commit_msg);
			my $ed = SVN::Git::Editor->new(
					{	r => $r_last,
						ra => $SVN,
						c => $c,
						svn_path => $SVN_PATH
					},
					$SVN->get_commit_editor(
						$log_msg->{msg},
						sub {
							libsvn_commit_cb(
								@_, $c,
								$log_msg->{msg},
								$r_last,
								$cmt_last)
						},
						@lock)
					);
			my $mods = libsvn_checkout_tree($cmt_last, $c, $ed);
			if (@$mods == 0) {
				print "No changes\nr$r_last = $cmt_last\n";
				$ed->abort_edit;
			} else {
				$ed->close_edit;
			}
			exit 0;
		}
		my ($r_new, $cmt_new, $no);
		while (<$fh>) {
			print $_;
			chomp;
			if (/^r(\d+) = ($sha1)$/o) {
				($r_new, $cmt_new) = ($1, $2);
			} elsif ($_ eq 'No changes') {
				$no = 1;
			}
		}
		close $fh or croak $!;
		if (! defined $r_new && ! defined $cmt_new) {
			unless ($no) {
				die "Failed to parse revision information\n";
			}
		} else {
			($r_last, $cmt_last) = ($r_new, $cmt_new);
		}
	}
	unlink $commit_msg;
}

sub show_ignore {
	$SVN_URL ||= file_to_s("$GIT_SVN_DIR/info/url");
	$_use_lib ? show_ignore_lib() : show_ignore_cmd();
}

sub show_ignore_cmd {
	require File::Find or die $!;
	if (defined $_revision) {
		die "-r/--revision option doesn't work unless the Perl SVN ",
			"libraries are used\n";
	}
	chdir $SVN_WC or croak $!;
	my %ign;
	File::Find::find({wanted=>sub{if(lstat $_ && -d _ && -d "$_/.svn"){
		s#^\./##;
		@{$ign{$_}} = svn_propget_base('svn:ignore', $_);
		}}, no_chdir=>1},'.');

	print "\n# /\n";
	foreach (@{$ign{'.'}}) { print '/',$_ if /\S/ }
	delete $ign{'.'};
	foreach my $i (sort keys %ign) {
		print "\n# ",$i,"\n";
		foreach (@{$ign{$i}}) { print '/',$i,'/',$_ if /\S/ }
	}
}

sub show_ignore_lib {
	my $repo;
	($repo, $SVN_PATH) = repo_path_split($SVN_URL);
	$SVN ||= libsvn_connect($repo);
	my $r = defined $_revision ? $_revision : $SVN->get_latest_revnum;
	libsvn_traverse_ignore(\*STDOUT, $SVN_PATH, $r);
}

sub graft_branches {
	my $gr_file = "$GIT_DIR/info/grafts";
	my ($grafts, $comments) = read_grafts($gr_file);
	my $gr_sha1;

	if (%$grafts) {
		# temporarily disable our grafts file to make this idempotent
		chomp($gr_sha1 = safe_qx(qw/git-hash-object -w/,$gr_file));
		rename $gr_file, "$gr_file~$gr_sha1" or croak $!;
	}

	my $l_map = read_url_paths();
	my @re = map { qr/$_/is } @_opt_m if @_opt_m;
	unless ($_no_default_regex) {
		push @re, (	qr/\b(?:merge|merging|merged)\s+(\S.+)/is,
				qr/\b(?:from|of)\s+(\S.+)/is );
	}
	foreach my $u (keys %$l_map) {
		if (@re) {
			foreach my $p (keys %{$l_map->{$u}}) {
				graft_merge_msg($grafts,$l_map,$u,$p);
			}
		}
		unless ($_no_graft_copy) {
			if ($_use_lib) {
				graft_file_copy_lib($grafts,$l_map,$u);
			} else {
				graft_file_copy_cmd($grafts,$l_map,$u);
			}
		}
	}

	write_grafts($grafts, $comments, $gr_file);
	unlink "$gr_file~$gr_sha1" if $gr_sha1;
}

sub multi_init {
	my $url = shift;
	$_trunk ||= 'trunk';
	$_trunk =~ s#/+$##;
	$url =~ s#/+$## if $url;
	if ($_trunk !~ m#^[a-z\+]+://#) {
		$_trunk = '/' . $_trunk if ($_trunk !~ m#^/#);
		unless ($url) {
			print STDERR "E: '$_trunk' is not a complete URL ",
				"and a separate URL is not specified\n";
			exit 1;
		}
		$_trunk = $url . $_trunk;
	}
	if ($GIT_SVN eq 'git-svn') {
		print "GIT_SVN_ID set to 'trunk' for $_trunk\n";
		$GIT_SVN = $ENV{GIT_SVN_ID} = 'trunk';
	}
	init_vars();
	init($_trunk);
	complete_url_ls_init($url, $_branches, '--branches/-b', '');
	complete_url_ls_init($url, $_tags, '--tags/-t', 'tags/');
}

sub multi_fetch {
	# try to do trunk first, since branches/tags
	# may be descended from it.
	if (-d "$GIT_DIR/svn/trunk") {
		print "Fetching trunk\n";
		defined(my $pid = fork) or croak $!;
		if (!$pid) {
			$GIT_SVN = $ENV{GIT_SVN_ID} = 'trunk';
			init_vars();
			fetch(@_);
			exit 0;
		}
		waitpid $pid, 0;
		croak $? if $?;
	}
	rec_fetch('', "$GIT_DIR/svn", @_);
}

sub show_log {
	my (@args) = @_;
	my ($r_min, $r_max);
	my $r_last = -1; # prevent dupes
	rload_authors() if $_authors;
	if (defined $TZ) {
		$ENV{TZ} = $TZ;
	} else {
		delete $ENV{TZ};
	}
	if (defined $_revision) {
		if ($_revision =~ /^(\d+):(\d+)$/) {
			($r_min, $r_max) = ($1, $2);
		} elsif ($_revision =~ /^\d+$/) {
			$r_min = $r_max = $_revision;
		} else {
			print STDERR "-r$_revision is not supported, use ",
				"standard \'git log\' arguments instead\n";
			exit 1;
		}
	}

	my $pid = open(my $log,'-|');
	defined $pid or croak $!;
	if (!$pid) {
		my @rl = (qw/git-log --abbrev-commit --pretty=raw
				--default/, "remotes/$GIT_SVN");
		push @rl, '--raw' if $_verbose;
		exec(@rl, @args) or croak $!;
	}
	setup_pager();
	my (@k, $c, $d);
	while (<$log>) {
		if (/^commit ($sha1_short)/o) {
			my $cmt = $1;
			if ($c && defined $c->{r} && $c->{r} != $r_last) {
				$r_last = $c->{r};
				process_commit($c, $r_min, $r_max, \@k) or
								goto out;
			}
			$d = undef;
			$c = { c => $cmt };
		} elsif (/^author (.+) (\d+) ([\-\+]?\d+)$/) {
			get_author_info($c, $1, $2, $3);
		} elsif (/^(?:tree|parent|committer) /) {
			# ignore
		} elsif (/^:\d{6} \d{6} $sha1_short/o) {
			push @{$c->{raw}}, $_;
		} elsif (/^diff /) {
			$d = 1;
			push @{$c->{diff}}, $_;
		} elsif ($d) {
			push @{$c->{diff}}, $_;
		} elsif (/^    (git-svn-id:.+)$/) {
			my ($url, $rev, $uuid) = extract_metadata($1);
			$c->{r} = $rev;
		} elsif (s/^    //) {
			push @{$c->{l}}, $_;
		}
	}
	if ($c && defined $c->{r} && $c->{r} != $r_last) {
		$r_last = $c->{r};
		process_commit($c, $r_min, $r_max, \@k);
	}
	if (@k) {
		my $swap = $r_max;
		$r_max = $r_min;
		$r_min = $swap;
		process_commit($_, $r_min, $r_max) foreach reverse @k;
	}
out:
	close $log;
	print '-' x72,"\n" unless $_incremental || $_oneline;
}

########################### utility functions #########################

sub rec_fetch {
	my ($pfx, $p, @args) = @_;
	my @dir;
	foreach (sort <$p/*>) {
		if (-r "$_/info/url") {
			$pfx .= '/' if $pfx && $pfx !~ m!/$!;
			my $id = $pfx . basename $_;
			next if $id eq 'trunk';
			print "Fetching $id\n";
			defined(my $pid = fork) or croak $!;
			if (!$pid) {
				$GIT_SVN = $ENV{GIT_SVN_ID} = $id;
				init_vars();
				fetch(@args);
				exit 0;
			}
			waitpid $pid, 0;
			croak $? if $?;
		} elsif (-d $_) {
			push @dir, $_;
		}
	}
	foreach (@dir) {
		my $x = $_;
		$x =~ s!^\Q$GIT_DIR\E/svn/!!;
		rec_fetch($x, $_);
	}
}

sub complete_url_ls_init {
	my ($url, $var, $switch, $pfx) = @_;
	unless ($var) {
		print STDERR "W: $switch not specified\n";
		return;
	}
	$var =~ s#/+$##;
	if ($var !~ m#^[a-z\+]+://#) {
		$var = '/' . $var if ($var !~ m#^/#);
		unless ($url) {
			print STDERR "E: '$var' is not a complete URL ",
				"and a separate URL is not specified\n";
			exit 1;
		}
		$var = $url . $var;
	}
	chomp(my @ls = $_use_lib ? libsvn_ls_fullurl($var)
				: safe_qx(qw/svn ls --non-interactive/, $var));
	my $old = $GIT_SVN;
	defined(my $pid = fork) or croak $!;
	if (!$pid) {
		foreach my $u (map { "$var/$_" } (grep m!/$!, @ls)) {
			$u =~ s#/+$##;
			if ($u !~ m!\Q$var\E/(.+)$!) {
				print STDERR "W: Unrecognized URL: $u\n";
				die "This should never happen\n";
			}
			my $id = $pfx.$1;
			print "init $u => $id\n";
			$GIT_SVN = $ENV{GIT_SVN_ID} = $id;
			init_vars();
			init($u);
		}
		exit 0;
	}
	waitpid $pid, 0;
	croak $? if $?;
}

sub common_prefix {
	my $paths = shift;
	my %common;
	foreach (@$paths) {
		my @tmp = split m#/#, $_;
		my $p = '';
		while (my $x = shift @tmp) {
			$p .= "/$x";
			$common{$p} ||= 0;
			$common{$p}++;
		}
	}
	foreach (sort {length $b <=> length $a} keys %common) {
		if ($common{$_} == @$paths) {
			return $_;
		}
	}
	return '';
}

# this isn't funky-filename safe, but good enough for now...
sub graft_file_copy_cmd {
	my ($grafts, $l_map, $u) = @_;
	my $paths = $l_map->{$u};
	my $pfx = common_prefix([keys %$paths]);
	$SVN_URL ||= $u.$pfx;
	my $pid = open my $fh, '-|';
	defined $pid or croak $!;
	unless ($pid) {
		my @exec = qw/svn log -v/;
		push @exec, "-r$_revision" if defined $_revision;
		exec @exec, $u.$pfx or croak $!;
	}
	my ($r, $mp) = (undef, undef);
	while (<$fh>) {
		chomp;
		if (/^\-{72}$/) {
			$mp = $r = undef;
		} elsif (/^r(\d+) \| /) {
			$r = $1 unless defined $r;
		} elsif (/^Changed paths:/) {
			$mp = 1;
		} elsif ($mp && m#^   [AR] /(\S.*?) \(from /(\S+?):(\d+)\)$#) {
			my ($p1, $p0, $r0) = ($1, $2, $3);
			my $c = find_graft_path_commit($paths, $p1, $r);
			next unless $c;
			find_graft_path_parents($grafts, $paths, $c, $p0, $r0);
		}
	}
}

sub graft_file_copy_lib {
	my ($grafts, $l_map, $u) = @_;
	my $tree_paths = $l_map->{$u};
	my $pfx = common_prefix([keys %$tree_paths]);
	my ($repo, $path) = repo_path_split($u.$pfx);
	$SVN_LOG ||= libsvn_connect($repo);
	$SVN ||= libsvn_connect($repo);

	my ($base, $head) = libsvn_parse_revision();
	my $inc = 1000;
	my ($min, $max) = ($base, $head < $base+$inc ? $head : $base+$inc);
	my $eh = $SVN::Error::handler;
	$SVN::Error::handler = \&libsvn_skip_unknown_revs;
	while (1) {
		my $pool = SVN::Pool->new;
		$SVN_LOG->get_log( "/$path", $min, $max, 0, 1, 1,
			sub {
				libsvn_graft_file_copies($grafts, $tree_paths,
							$path, @_);
			}, $pool);
		$pool->clear;
		last if ($max >= $head);
		$min = $max + 1;
		$max += $inc;
		$max = $head if ($max > $head);
	}
	$SVN::Error::handler = $eh;
}

sub process_merge_msg_matches {
	my ($grafts, $l_map, $u, $p, $c, @matches) = @_;
	my (@strong, @weak);
	foreach (@matches) {
		# merging with ourselves is not interesting
		next if $_ eq $p;
		if ($l_map->{$u}->{$_}) {
			push @strong, $_;
		} else {
			push @weak, $_;
		}
	}
	foreach my $w (@weak) {
		last if @strong;
		# no exact match, use branch name as regexp.
		my $re = qr/\Q$w\E/i;
		foreach (keys %{$l_map->{$u}}) {
			if (/$re/) {
				push @strong, $_;
				last;
			}
		}
		last if @strong;
		$w = basename($w);
		$re = qr/\Q$w\E/i;
		foreach (keys %{$l_map->{$u}}) {
			if (/$re/) {
				push @strong, $_;
				last;
			}
		}
	}
	my ($rev) = ($c->{m} =~ /^git-svn-id:\s(?:\S+?)\@(\d+)
					\s(?:[a-f\d\-]+)$/xsm);
	unless (defined $rev) {
		($rev) = ($c->{m} =~/^git-svn-id:\s(\d+)
					\@(?:[a-f\d\-]+)/xsm);
		return unless defined $rev;
	}
	foreach my $m (@strong) {
		my ($r0, $s0) = find_rev_before($rev, $m);
		$grafts->{$c->{c}}->{$s0} = 1 if defined $s0;
	}
}

sub graft_merge_msg {
	my ($grafts, $l_map, $u, $p, @re) = @_;

	my $x = $l_map->{$u}->{$p};
	my $rl = rev_list_raw($x);
	while (my $c = next_rev_list_entry($rl)) {
		foreach my $re (@re) {
			my (@br) = ($c->{m} =~ /$re/g);
			next unless @br;
			process_merge_msg_matches($grafts,$l_map,$u,$p,$c,@br);
		}
	}
}

sub read_uuid {
	return if $SVN_UUID;
	if ($_use_lib) {
		my $pool = SVN::Pool->new;
		$SVN_UUID = $SVN->get_uuid($pool);
		$pool->clear;
	} else {
		my $info = shift || svn_info('.');
		$SVN_UUID = $info->{'Repository UUID'} or
					croak "Repository UUID unreadable\n";
	}
	s_to_file($SVN_UUID,"$GIT_SVN_DIR/info/uuid");
}

sub quiet_run {
	my $pid = fork;
	defined $pid or croak $!;
	if (!$pid) {
		open my $null, '>', '/dev/null' or croak $!;
		open STDERR, '>&', $null or croak $!;
		open STDOUT, '>&', $null or croak $!;
		exec @_ or croak $!;
	}
	waitpid $pid, 0;
	return $?;
}

sub repo_path_split {
	my $full_url = shift;
	$full_url =~ s#/+$##;

	foreach (@repo_path_split_cache) {
		if ($full_url =~ s#$_##) {
			my $u = $1;
			$full_url =~ s#^/+##;
			return ($u, $full_url);
		}
	}

	my ($url, $path) = ($full_url =~ m!^([a-z\+]+://[^/]*)(.*)$!i);
	$path =~ s#^/+##;
	my @paths = split(m#/+#, $path);

	if ($_use_lib) {
		while (1) {
			$SVN = libsvn_connect($url);
			last if (defined $SVN &&
				defined eval { $SVN->get_latest_revnum });
			my $n = shift @paths || last;
			$url .= "/$n";
		}
	} else {
		while (quiet_run(qw/svn ls --non-interactive/, $url)) {
			my $n = shift @paths || last;
			$url .= "/$n";
		}
	}
	push @repo_path_split_cache, qr/^(\Q$url\E)/;
	$path = join('/',@paths);
	return ($url, $path);
}

sub setup_git_svn {
	defined $SVN_URL or croak "SVN repository location required\n";
	unless (-d $GIT_DIR) {
		croak "GIT_DIR=$GIT_DIR does not exist!\n";
	}
	mkpath([$GIT_SVN_DIR]);
	mkpath(["$GIT_SVN_DIR/info"]);
	open my $fh, '>>',$REVDB or croak $!;
	close $fh;
	s_to_file($SVN_URL,"$GIT_SVN_DIR/info/url");

	open my $fd, '>>', "$GIT_SVN_DIR/info/exclude" or croak $!;
	print $fd '.svn',"\n";
	close $fd or croak $!;
	my ($url, $path) = repo_path_split($SVN_URL);
	s_to_file($url, "$GIT_SVN_DIR/info/repo_url");
	s_to_file($path, "$GIT_SVN_DIR/info/repo_path");
}

sub assert_svn_wc_clean {
	return if $_use_lib;
	my ($svn_rev) = @_;
	croak "$svn_rev is not an integer!\n" unless ($svn_rev =~ /^\d+$/);
	my $lcr = svn_info('.')->{'Last Changed Rev'};
	if ($svn_rev != $lcr) {
		print STDERR "Checking for copy-tree ... ";
		my @diff = grep(/^Index: /,(safe_qx(qw(svn diff),
						"-r$lcr:$svn_rev")));
		if (@diff) {
			croak "Nope!  Expected r$svn_rev, got r$lcr\n";
		} else {
			print STDERR "OK!\n";
		}
	}
	my @status = grep(!/^Performing status on external/,(`svn status`));
	@status = grep(!/^\s*$/,@status);
	if (scalar @status) {
		print STDERR "Tree ($SVN_WC) is not clean:\n";
		print STDERR $_ foreach @status;
		croak;
	}
}

sub get_tree_from_treeish {
	my ($treeish) = @_;
	croak "Not a sha1: $treeish\n" unless $treeish =~ /^$sha1$/o;
	chomp(my $type = `git-cat-file -t $treeish`);
	my $expected;
	while ($type eq 'tag') {
		chomp(($treeish, $type) = `git-cat-file tag $treeish`);
	}
	if ($type eq 'commit') {
		$expected = (grep /^tree /,`git-cat-file commit $treeish`)[0];
		($expected) = ($expected =~ /^tree ($sha1)$/);
		die "Unable to get tree from $treeish\n" unless $expected;
	} elsif ($type eq 'tree') {
		$expected = $treeish;
	} else {
		die "$treeish is a $type, expected tree, tag or commit\n";
	}
	return $expected;
}

sub assert_tree {
	return if $_use_lib;
	my ($treeish) = @_;
	my $expected = get_tree_from_treeish($treeish);

	my $tmpindex = $GIT_SVN_INDEX.'.assert-tmp';
	if (-e $tmpindex) {
		unlink $tmpindex or croak $!;
	}
	my $old_index = set_index($tmpindex);
	index_changes(1);
	chomp(my $tree = `git-write-tree`);
	restore_index($old_index);
	if ($tree ne $expected) {
		croak "Tree mismatch, Got: $tree, Expected: $expected\n";
	}
	unlink $tmpindex;
}

sub parse_diff_tree {
	my $diff_fh = shift;
	local $/ = "\0";
	my $state = 'meta';
	my @mods;
	while (<$diff_fh>) {
		chomp $_; # this gets rid of the trailing "\0"
		if ($state eq 'meta' && /^:(\d{6})\s(\d{6})\s
					$sha1\s($sha1)\s([MTCRAD])\d*$/xo) {
			push @mods, {	mode_a => $1, mode_b => $2,
					sha1_b => $3, chg => $4 };
			if ($4 =~ /^(?:C|R)$/) {
				$state = 'file_a';
			} else {
				$state = 'file_b';
			}
		} elsif ($state eq 'file_a') {
			my $x = $mods[$#mods] or croak "Empty array\n";
			if ($x->{chg} !~ /^(?:C|R)$/) {
				croak "Error parsing $_, $x->{chg}\n";
			}
			$x->{file_a} = $_;
			$state = 'file_b';
		} elsif ($state eq 'file_b') {
			my $x = $mods[$#mods] or croak "Empty array\n";
			if (exists $x->{file_a} && $x->{chg} !~ /^(?:C|R)$/) {
				croak "Error parsing $_, $x->{chg}\n";
			}
			if (!exists $x->{file_a} && $x->{chg} =~ /^(?:C|R)$/) {
				croak "Error parsing $_, $x->{chg}\n";
			}
			$x->{file_b} = $_;
			$state = 'meta';
		} else {
			croak "Error parsing $_\n";
		}
	}
	close $diff_fh or croak $!;

	return \@mods;
}

sub svn_check_prop_executable {
	my $m = shift;
	return if -l $m->{file_b};
	if ($m->{mode_b} =~ /755$/) {
		chmod((0755 &~ umask),$m->{file_b}) or croak $!;
		if ($m->{mode_a} !~ /755$/) {
			sys(qw(svn propset svn:executable 1), $m->{file_b});
		}
		-x $m->{file_b} or croak "$m->{file_b} is not executable!\n";
	} elsif ($m->{mode_b} !~ /755$/ && $m->{mode_a} =~ /755$/) {
		sys(qw(svn propdel svn:executable), $m->{file_b});
		chmod((0644 &~ umask),$m->{file_b}) or croak $!;
		-x $m->{file_b} and croak "$m->{file_b} is executable!\n";
	}
}

sub svn_ensure_parent_path {
	my $dir_b = dirname(shift);
	svn_ensure_parent_path($dir_b) if ($dir_b ne File::Spec->curdir);
	mkpath([$dir_b]) unless (-d $dir_b);
	sys(qw(svn add -N), $dir_b) unless (-d "$dir_b/.svn");
}

sub precommit_check {
	my $mods = shift;
	my (%rm_file, %rmdir_check, %added_check);

	my %o = ( D => 0, R => 1, C => 2, A => 3, M => 3, T => 3 );
	foreach my $m (sort { $o{$a->{chg}} <=> $o{$b->{chg}} } @$mods) {
		if ($m->{chg} eq 'R') {
			if (-d $m->{file_b}) {
				err_dir_to_file("$m->{file_a} => $m->{file_b}");
			}
			# dir/$file => dir/file/$file
			my $dirname = dirname($m->{file_b});
			while ($dirname ne File::Spec->curdir) {
				if ($dirname ne $m->{file_a}) {
					$dirname = dirname($dirname);
					next;
				}
				err_file_to_dir("$m->{file_a} => $m->{file_b}");
			}
			# baz/zzz => baz (baz is a file)
			$dirname = dirname($m->{file_a});
			while ($dirname ne File::Spec->curdir) {
				if ($dirname ne $m->{file_b}) {
					$dirname = dirname($dirname);
					next;
				}
				err_dir_to_file("$m->{file_a} => $m->{file_b}");
			}
		}
		if ($m->{chg} =~ /^(D|R)$/) {
			my $t = $1 eq 'D' ? 'file_b' : 'file_a';
			$rm_file{ $m->{$t} } = 1;
			my $dirname = dirname( $m->{$t} );
			my $basename = basename( $m->{$t} );
			$rmdir_check{$dirname}->{$basename} = 1;
		} elsif ($m->{chg} =~ /^(?:A|C)$/) {
			if (-d $m->{file_b}) {
				err_dir_to_file($m->{file_b});
			}
			my $dirname = dirname( $m->{file_b} );
			my $basename = basename( $m->{file_b} );
			$added_check{$dirname}->{$basename} = 1;
			while ($dirname ne File::Spec->curdir) {
				if ($rm_file{$dirname}) {
					err_file_to_dir($m->{file_b});
				}
				$dirname = dirname $dirname;
			}
		}
	}
	return (\%rmdir_check, \%added_check);

	sub err_dir_to_file {
		my $file = shift;
		print STDERR "Node change from directory to file ",
				"is not supported by Subversion: ",$file,"\n";
		exit 1;
	}
	sub err_file_to_dir {
		my $file = shift;
		print STDERR "Node change from file to directory ",
				"is not supported by Subversion: ",$file,"\n";
		exit 1;
	}
}


sub get_diff {
	my ($from, $treeish) = @_;
	assert_tree($from);
	print "diff-tree $from $treeish\n";
	my $pid = open my $diff_fh, '-|';
	defined $pid or croak $!;
	if ($pid == 0) {
		my @diff_tree = qw(git-diff-tree -z -r);
		if ($_cp_similarity) {
			push @diff_tree, "-C$_cp_similarity";
		} else {
			push @diff_tree, '-C';
		}
		push @diff_tree, '--find-copies-harder' if $_find_copies_harder;
		push @diff_tree, "-l$_l" if defined $_l;
		exec(@diff_tree, $from, $treeish) or croak $!;
	}
	return parse_diff_tree($diff_fh);
}

sub svn_checkout_tree {
	my ($from, $treeish) = @_;
	my $mods = get_diff($from->{commit}, $treeish);
	return $mods unless (scalar @$mods);
	my ($rm, $add) = precommit_check($mods);

	my %o = ( D => 1, R => 0, C => -1, A => 3, M => 3, T => 3 );
	foreach my $m (sort { $o{$a->{chg}} <=> $o{$b->{chg}} } @$mods) {
		if ($m->{chg} eq 'C') {
			svn_ensure_parent_path( $m->{file_b} );
			sys(qw(svn cp),		$m->{file_a}, $m->{file_b});
			apply_mod_line_blob($m);
			svn_check_prop_executable($m);
		} elsif ($m->{chg} eq 'D') {
			sys(qw(svn rm --force), $m->{file_b});
		} elsif ($m->{chg} eq 'R') {
			svn_ensure_parent_path( $m->{file_b} );
			sys(qw(svn mv --force), $m->{file_a}, $m->{file_b});
			apply_mod_line_blob($m);
			svn_check_prop_executable($m);
		} elsif ($m->{chg} eq 'M') {
			apply_mod_line_blob($m);
			svn_check_prop_executable($m);
		} elsif ($m->{chg} eq 'T') {
			sys(qw(svn rm --force),$m->{file_b});
			apply_mod_line_blob($m);
			sys(qw(svn add --force), $m->{file_b});
			svn_check_prop_executable($m);
		} elsif ($m->{chg} eq 'A') {
			svn_ensure_parent_path( $m->{file_b} );
			apply_mod_line_blob($m);
			sys(qw(svn add --force), $m->{file_b});
			svn_check_prop_executable($m);
		} else {
			croak "Invalid chg: $m->{chg}\n";
		}
	}

	assert_tree($treeish);
	if ($_rmdir) { # remove empty directories
		handle_rmdir($rm, $add);
	}
	assert_tree($treeish);
	return $mods;
}

sub libsvn_checkout_tree {
	my ($from, $treeish, $ed) = @_;
	my $mods = get_diff($from, $treeish);
	return $mods unless (scalar @$mods);
	my %o = ( D => 1, R => 0, C => -1, A => 3, M => 3, T => 3 );
	foreach my $m (sort { $o{$a->{chg}} <=> $o{$b->{chg}} } @$mods) {
		my $f = $m->{chg};
		if (defined $o{$f}) {
			$ed->$f($m);
		} else {
			croak "Invalid change type: $f\n";
		}
	}
	$ed->rmdirs if $_rmdir;
	return $mods;
}

# svn ls doesn't work with respect to the current working tree, but what's
# in the repository.  There's not even an option for it... *sigh*
# (added files don't show up and removed files remain in the ls listing)
sub svn_ls_current {
	my ($dir, $rm, $add) = @_;
	chomp(my @ls = safe_qx('svn','ls',$dir));
	my @ret = ();
	foreach (@ls) {
		s#/$##; # trailing slashes are evil
		push @ret, $_ unless $rm->{$dir}->{$_};
	}
	if (exists $add->{$dir}) {
		push @ret, keys %{$add->{$dir}};
	}
	return \@ret;
}

sub handle_rmdir {
	my ($rm, $add) = @_;

	foreach my $dir (sort {length $b <=> length $a} keys %$rm) {
		my $ls = svn_ls_current($dir, $rm, $add);
		next if (scalar @$ls);
		sys(qw(svn rm --force),$dir);

		my $dn = dirname $dir;
		$rm->{ $dn }->{ basename $dir } = 1;
		$ls = svn_ls_current($dn, $rm, $add);
		while (scalar @$ls == 0 && $dn ne File::Spec->curdir) {
			sys(qw(svn rm --force),$dn);
			$dir = basename $dn;
			$dn = dirname $dn;
			$rm->{ $dn }->{ $dir } = 1;
			$ls = svn_ls_current($dn, $rm, $add);
		}
	}
}

sub get_commit_message {
	my ($commit, $commit_msg) = (@_);
	my %log_msg = ( msg => '' );
	open my $msg, '>', $commit_msg or croak $!;

	print "commit: $commit\n";
	chomp(my $type = `git-cat-file -t $commit`);
	if ($type eq 'commit') {
		my $pid = open my $msg_fh, '-|';
		defined $pid or croak $!;

		if ($pid == 0) {
			exec(qw(git-cat-file commit), $commit) or croak $!;
		}
		my $in_msg = 0;
		while (<$msg_fh>) {
			if (!$in_msg) {
				$in_msg = 1 if (/^\s*$/);
			} elsif (/^git-svn-id: /) {
				# skip this, we regenerate the correct one
				# on re-fetch anyways
			} else {
				print $msg $_ or croak $!;
			}
		}
		close $msg_fh or croak $!;
	}
	close $msg or croak $!;

	if ($_edit || ($type eq 'tree')) {
		my $editor = $ENV{VISUAL} || $ENV{EDITOR} || 'vi';
		system($editor, $commit_msg);
	}

	# file_to_s removes all trailing newlines, so just use chomp() here:
	open $msg, '<', $commit_msg or croak $!;
	{ local $/; chomp($log_msg{msg} = <$msg>); }
	close $msg or croak $!;

	return \%log_msg;
}

sub svn_commit_tree {
	my ($last, $commit) = @_;
	my $commit_msg = "$GIT_SVN_DIR/.svn-commit.tmp.$$";
	my $log_msg = get_commit_message($commit, $commit_msg);
	my ($oneline) = ($log_msg->{msg} =~ /([^\n\r]+)/);
	print "Committing $commit: $oneline\n";

	if (defined $LC_ALL) {
		$ENV{LC_ALL} = $LC_ALL;
	} else {
		delete $ENV{LC_ALL};
	}
	my @ci_output = safe_qx(qw(svn commit -F),$commit_msg);
	$ENV{LC_ALL} = 'C';
	unlink $commit_msg;
	my ($committed) = ($ci_output[$#ci_output] =~ /(\d+)/);
	if (!defined $committed) {
		my $out = join("\n",@ci_output);
		print STDERR "W: Trouble parsing \`svn commit' output:\n\n",
				$out, "\n\nAssuming English locale...";
		($committed) = ($out =~ /^Committed revision \d+\./sm);
		defined $committed or die " FAILED!\n",
			"Commit output failed to parse committed revision!\n",
		print STDERR " OK\n";
	}

	my @svn_up = qw(svn up);
	push @svn_up, '--ignore-externals' unless $_no_ignore_ext;
	if ($_optimize_commits && ($committed == ($last->{revision} + 1))) {
		push @svn_up, "-r$committed";
		sys(@svn_up);
		my $info = svn_info('.');
		my $date = $info->{'Last Changed Date'} or die "Missing date\n";
		if ($info->{'Last Changed Rev'} != $committed) {
			croak "$info->{'Last Changed Rev'} != $committed\n"
		}
		my ($Y,$m,$d,$H,$M,$S,$tz) = ($date =~
					/(\d{4})\-(\d\d)\-(\d\d)\s
					 (\d\d)\:(\d\d)\:(\d\d)\s([\-\+]\d+)/x)
					 or croak "Failed to parse date: $date\n";
		$log_msg->{date} = "$tz $Y-$m-$d $H:$M:$S";
		$log_msg->{author} = $info->{'Last Changed Author'};
		$log_msg->{revision} = $committed;
		$log_msg->{msg} .= "\n";
		$log_msg->{parents} = [ $last->{commit} ];
		$log_msg->{commit} = git_commit($log_msg, $commit);
		return $log_msg;
	}
	# resync immediately
	push @svn_up, "-r$last->{revision}";
	sys(@svn_up);
	return fetch("$committed=$commit");
}

sub rev_list_raw {
	my (@args) = @_;
	my $pid = open my $fh, '-|';
	defined $pid or croak $!;
	if (!$pid) {
		exec(qw/git-rev-list --pretty=raw/, @args) or croak $!;
	}
	return { fh => $fh, t => { } };
}

sub next_rev_list_entry {
	my $rl = shift;
	my $fh = $rl->{fh};
	my $x = $rl->{t};
	while (<$fh>) {
		if (/^commit ($sha1)$/o) {
			if ($x->{c}) {
				$rl->{t} = { c => $1 };
				return $x;
			} else {
				$x->{c} = $1;
			}
		} elsif (/^parent ($sha1)$/o) {
			$x->{p}->{$1} = 1;
		} elsif (s/^    //) {
			$x->{m} ||= '';
			$x->{m} .= $_;
		}
	}
	return ($x != $rl->{t}) ? $x : undef;
}

# read the entire log into a temporary file (which is removed ASAP)
# and store the file handle + parser state
sub svn_log_raw {
	my (@log_args) = @_;
	my $log_fh = IO::File->new_tmpfile or croak $!;
	my $pid = fork;
	defined $pid or croak $!;
	if (!$pid) {
		open STDOUT, '>&', $log_fh or croak $!;
		exec (qw(svn log), @log_args) or croak $!
	}
	waitpid $pid, 0;
	croak $? if $?;
	seek $log_fh, 0, 0 or croak $!;
	return { state => 'sep', fh => $log_fh };
}

sub next_log_entry {
	my $log = shift; # retval of svn_log_raw()
	my $ret = undef;
	my $fh = $log->{fh};

	while (<$fh>) {
		chomp;
		if (/^\-{72}$/) {
			if ($log->{state} eq 'msg') {
				if ($ret->{lines}) {
					$ret->{msg} .= $_."\n";
					unless(--$ret->{lines}) {
						$log->{state} = 'sep';
					}
				} else {
					croak "Log parse error at: $_\n",
						$ret->{revision},
						"\n";
				}
				next;
			}
			if ($log->{state} ne 'sep') {
				croak "Log parse error at: $_\n",
					"state: $log->{state}\n",
					$ret->{revision},
					"\n";
			}
			$log->{state} = 'rev';

			# if we have an empty log message, put something there:
			if ($ret) {
				$ret->{msg} ||= "\n";
				delete $ret->{lines};
				return $ret;
			}
			next;
		}
		if ($log->{state} eq 'rev' && s/^r(\d+)\s*\|\s*//) {
			my $rev = $1;
			my ($author, $date, $lines) = split(/\s*\|\s*/, $_, 3);
			($lines) = ($lines =~ /(\d+)/);
			my ($Y,$m,$d,$H,$M,$S,$tz) = ($date =~
					/(\d{4})\-(\d\d)\-(\d\d)\s
					 (\d\d)\:(\d\d)\:(\d\d)\s([\-\+]\d+)/x)
					 or croak "Failed to parse date: $date\n";
			$ret = {	revision => $rev,
					date => "$tz $Y-$m-$d $H:$M:$S",
					author => $author,
					lines => $lines,
					msg => '' };
			if (defined $_authors && ! defined $users{$author}) {
				die "Author: $author not defined in ",
						"$_authors file\n";
			}
			$log->{state} = 'msg_start';
			next;
		}
		# skip the first blank line of the message:
		if ($log->{state} eq 'msg_start' && /^$/) {
			$log->{state} = 'msg';
		} elsif ($log->{state} eq 'msg') {
			if ($ret->{lines}) {
				$ret->{msg} .= $_."\n";
				unless (--$ret->{lines}) {
					$log->{state} = 'sep';
				}
			} else {
				croak "Log parse error at: $_\n",
					$ret->{revision},"\n";
			}
		}
	}
	return $ret;
}

sub svn_info {
	my $url = shift || $SVN_URL;

	my $pid = open my $info_fh, '-|';
	defined $pid or croak $!;

	if ($pid == 0) {
		exec(qw(svn info),$url) or croak $!;
	}

	my $ret = {};
	# only single-lines seem to exist in svn info output
	while (<$info_fh>) {
		chomp $_;
		if (m#^([^:]+)\s*:\s*(\S.*)$#) {
			$ret->{$1} = $2;
			push @{$ret->{-order}}, $1;
		}
	}
	close $info_fh or croak $!;
	return $ret;
}

sub sys { system(@_) == 0 or croak $? }

sub eol_cp {
	my ($from, $to) = @_;
	my $es = svn_propget_base('svn:eol-style', $to);
	open my $rfd, '<', $from or croak $!;
	binmode $rfd or croak $!;
	open my $wfd, '>', $to or croak $!;
	binmode $wfd or croak $!;
	eol_cp_fd($rfd, $wfd, $es);
	close $rfd or croak $!;
	close $wfd or croak $!;
}

sub eol_cp_fd {
	my ($rfd, $wfd, $es) = @_;
	my $eol = defined $es ? $EOL{$es} : undef;
	my $buf;
	use bytes;
	while (1) {
		my ($r, $w, $t);
		defined($r = sysread($rfd, $buf, 4096)) or croak $!;
		return unless $r;
		if ($eol) {
			if ($buf =~ /\015$/) {
				my $c;
				defined($r = sysread($rfd,$c,1)) or croak $!;
				$buf .= $c if $r > 0;
			}
			$buf =~ s/(?:\015\012|\015|\012)/$eol/gs;
			$r = length($buf);
		}
		for ($w = 0; $w < $r; $w += $t) {
			$t = syswrite($wfd, $buf, $r - $w, $w) or croak $!;
		}
	}
	no bytes;
}

sub do_update_index {
	my ($z_cmd, $cmd, $no_text_base) = @_;

	my $z = open my $p, '-|';
	defined $z or croak $!;
	unless ($z) { exec @$z_cmd or croak $! }

	my $pid = open my $ui, '|-';
	defined $pid or croak $!;
	unless ($pid) {
		exec('git-update-index',"--$cmd",'-z','--stdin') or croak $!;
	}
	local $/ = "\0";
	while (my $x = <$p>) {
		chomp $x;
		if (!$no_text_base && lstat $x && ! -l _ &&
				svn_propget_base('svn:keywords', $x)) {
			my $mode = -x _ ? 0755 : 0644;
			my ($v,$d,$f) = File::Spec->splitpath($x);
			my $tb = File::Spec->catfile($d, '.svn', 'tmp',
						'text-base',"$f.svn-base");
			$tb =~ s#^/##;
			unless (-f $tb) {
				$tb = File::Spec->catfile($d, '.svn',
						'text-base',"$f.svn-base");
				$tb =~ s#^/##;
			}
			unlink $x or croak $!;
			eol_cp($tb, $x);
			chmod(($mode &~ umask), $x) or croak $!;
		}
		print $ui $x,"\0";
	}
	close $ui or croak $!;
}

sub index_changes {
	return if $_use_lib;
	my $no_text_base = shift;
	do_update_index([qw/git-diff-files --name-only -z/],
			'remove',
			$no_text_base);
	do_update_index([qw/git-ls-files -z --others/,
				"--exclude-from=$GIT_SVN_DIR/info/exclude"],
			'add',
			$no_text_base);
}

sub s_to_file {
	my ($str, $file, $mode) = @_;
	open my $fd,'>',$file or croak $!;
	print $fd $str,"\n" or croak $!;
	close $fd or croak $!;
	chmod ($mode &~ umask, $file) if (defined $mode);
}

sub file_to_s {
	my $file = shift;
	open my $fd,'<',$file or croak "$!: file: $file\n";
	local $/;
	my $ret = <$fd>;
	close $fd or croak $!;
	$ret =~ s/\s*$//s;
	return $ret;
}

sub assert_revision_unknown {
	my $r = shift;
	if (my $c = revdb_get($REVDB, $r)) {
		croak "$r = $c already exists! Why are we refetching it?";
	}
}

sub trees_eq {
	my ($x, $y) = @_;
	my @x = safe_qx('git-cat-file','commit',$x);
	my @y = safe_qx('git-cat-file','commit',$y);
	if (($y[0] ne $x[0]) || $x[0] !~ /^tree $sha1\n$/
				|| $y[0] !~ /^tree $sha1\n$/) {
		print STDERR "Trees not equal: $y[0] != $x[0]\n";
		return 0
	}
	return 1;
}

sub git_commit {
	my ($log_msg, @parents) = @_;
	assert_revision_unknown($log_msg->{revision});
	map_tree_joins() if (@_branch_from && !%tree_map);

	my (@tmp_parents, @exec_parents, %seen_parent);
	if (my $lparents = $log_msg->{parents}) {
		@tmp_parents = @$lparents
	}
	# commit parents can be conditionally bound to a particular
	# svn revision via: "svn_revno=commit_sha1", filter them out here:
	foreach my $p (@parents) {
		next unless defined $p;
		if ($p =~ /^(\d+)=($sha1_short)$/o) {
			if ($1 == $log_msg->{revision}) {
				push @tmp_parents, $2;
			}
		} else {
			push @tmp_parents, $p if $p =~ /$sha1_short/o;
		}
	}
	my $tree = $log_msg->{tree};
	if (!defined $tree) {
		my $index = set_index($GIT_SVN_INDEX);
		index_changes();
		chomp($tree = `git-write-tree`);
		croak $? if $?;
		restore_index($index);
	}
	if (exists $tree_map{$tree}) {
		push @tmp_parents, @{$tree_map{$tree}};
	}
	foreach (@tmp_parents) {
		next if $seen_parent{$_};
		$seen_parent{$_} = 1;
		push @exec_parents, $_;
		# MAXPARENT is defined to 16 in commit-tree.c:
		last if @exec_parents > 16;
	}

	defined(my $pid = open my $out_fh, '-|') or croak $!;
	if ($pid == 0) {
		my $msg_fh = IO::File->new_tmpfile or croak $!;
		print $msg_fh $log_msg->{msg}, "\ngit-svn-id: ",
					"$SVN_URL\@$log_msg->{revision}",
					" $SVN_UUID\n" or croak $!;
		$msg_fh->flush == 0 or croak $!;
		seek $msg_fh, 0, 0 or croak $!;
		set_commit_env($log_msg);
		my @exec = ('git-commit-tree',$tree);
		push @exec, '-p', $_  foreach @exec_parents;
		open STDIN, '<&', $msg_fh or croak $!;
		exec @exec or croak $!;
	}
	chomp(my $commit = do { local $/; <$out_fh> });
	close $out_fh or croak $?;
	if ($commit !~ /^$sha1$/o) {
		croak "Failed to commit, invalid sha1: $commit\n";
	}
	my @update_ref = ('git-update-ref',"refs/remotes/$GIT_SVN",$commit);
	if (my $primary_parent = shift @exec_parents) {
		quiet_run(qw/git-rev-parse --verify/,"refs/remotes/$GIT_SVN^0");
		push @update_ref, $primary_parent unless $?;
	}
	sys(@update_ref);
	revdb_set($REVDB, $log_msg->{revision}, $commit);

	# this output is read via pipe, do not change:
	print "r$log_msg->{revision} = $commit\n";
	if ($_repack && (--$_repack_nr == 0)) {
		$_repack_nr = $_repack;
		sys("git repack $_repack_flags");
	}
	return $commit;
}

sub set_commit_env {
	my ($log_msg) = @_;
	my $author = $log_msg->{author};
	if (!defined $author || length $author == 0) {
		$author = '(no author)';
	}
	my ($name,$email) = defined $users{$author} ?  @{$users{$author}}
				: ($author,"$author\@$SVN_UUID");
	$ENV{GIT_AUTHOR_NAME} = $ENV{GIT_COMMITTER_NAME} = $name;
	$ENV{GIT_AUTHOR_EMAIL} = $ENV{GIT_COMMITTER_EMAIL} = $email;
	$ENV{GIT_AUTHOR_DATE} = $ENV{GIT_COMMITTER_DATE} = $log_msg->{date};
}

sub apply_mod_line_blob {
	my $m = shift;
	if ($m->{mode_b} =~ /^120/) {
		blob_to_symlink($m->{sha1_b}, $m->{file_b});
	} else {
		blob_to_file($m->{sha1_b}, $m->{file_b});
	}
}

sub blob_to_symlink {
	my ($blob, $link) = @_;
	defined $link or croak "\$link not defined!\n";
	croak "Not a sha1: $blob\n" unless $blob =~ /^$sha1$/o;
	if (-l $link || -f _) {
		unlink $link or croak $!;
	}

	my $dest = `git-cat-file blob $blob`; # no newline, so no chomp
	symlink $dest, $link or croak $!;
}

sub blob_to_file {
	my ($blob, $file) = @_;
	defined $file or croak "\$file not defined!\n";
	croak "Not a sha1: $blob\n" unless $blob =~ /^$sha1$/o;
	if (-l $file || -f _) {
		unlink $file or croak $!;
	}

	open my $blob_fh, '>', $file or croak "$!: $file\n";
	my $pid = fork;
	defined $pid or croak $!;

	if ($pid == 0) {
		open STDOUT, '>&', $blob_fh or croak $!;
		exec('git-cat-file','blob',$blob) or croak $!;
	}
	waitpid $pid, 0;
	croak $? if $?;

	close $blob_fh or croak $!;
}

sub safe_qx {
	my $pid = open my $child, '-|';
	defined $pid or croak $!;
	if ($pid == 0) {
		exec(@_) or croak $!;
	}
	my @ret = (<$child>);
	close $child or croak $?;
	die $? if $?; # just in case close didn't error out
	return wantarray ? @ret : join('',@ret);
}

sub svn_compat_check {
	my @co_help = safe_qx(qw(svn co -h));
	unless (grep /ignore-externals/,@co_help) {
		print STDERR "W: Installed svn version does not support ",
				"--ignore-externals\n";
		$_no_ignore_ext = 1;
	}
	if (grep /usage: checkout URL\[\@REV\]/,@co_help) {
		$_svn_co_url_revs = 1;
	}
	if (grep /\[TARGET\[\@REV\]\.\.\.\]/, `svn propget -h`) {
		$_svn_pg_peg_revs = 1;
	}

	# I really, really hope nobody hits this...
	unless (grep /stop-on-copy/, (safe_qx(qw(svn log -h)))) {
		print STDERR <<'';
W: The installed svn version does not support the --stop-on-copy flag in
   the log command.
   Lets hope the directory you're tracking is not a branch or tag
   and was never moved within the repository...

		$_no_stop_copy = 1;
	}
}

# *sigh*, new versions of svn won't honor -r<rev> without URL@<rev>,
# (and they won't honor URL@<rev> without -r<rev>, too!)
sub svn_cmd_checkout {
	my ($url, $rev, $dir) = @_;
	my @cmd = ('svn','co', "-r$rev");
	push @cmd, '--ignore-externals' unless $_no_ignore_ext;
	$url .= "\@$rev" if $_svn_co_url_revs;
	sys(@cmd, $url, $dir);
}

sub check_upgrade_needed {
	my $old = eval {
		my $pid = open my $child, '-|';
		defined $pid or croak $!;
		if ($pid == 0) {
			close STDERR;
			exec('git-rev-parse',"$GIT_SVN-HEAD") or croak $!;
		}
		my @ret = (<$child>);
		close $child or croak $?;
		die $? if $?; # just in case close didn't error out
		return wantarray ? @ret : join('',@ret);
	};
	return unless $old;
	my $head = eval { safe_qx('git-rev-parse',"refs/remotes/$GIT_SVN") };
	if ($@ || !$head) {
		print STDERR "Please run: $0 rebuild --upgrade\n";
		exit 1;
	}
}

# fills %tree_map with a reverse mapping of trees to commits.  Useful
# for finding parents to commit on.
sub map_tree_joins {
	my %seen;
	foreach my $br (@_branch_from) {
		my $pid = open my $pipe, '-|';
		defined $pid or croak $!;
		if ($pid == 0) {
			exec(qw(git-rev-list --topo-order --pretty=raw), $br)
								or croak $!;
		}
		while (<$pipe>) {
			if (/^commit ($sha1)$/o) {
				my $commit = $1;

				# if we've seen a commit,
				# we've seen its parents
				last if $seen{$commit};
				my ($tree) = (<$pipe> =~ /^tree ($sha1)$/o);
				unless (defined $tree) {
					die "Failed to parse commit $commit\n";
				}
				push @{$tree_map{$tree}}, $commit;
				$seen{$commit} = 1;
			}
		}
		close $pipe; # we could be breaking the pipe early
	}
}

sub load_all_refs {
	if (@_branch_from) {
		print STDERR '--branch|-b parameters are ignored when ',
			"--branch-all-refs|-B is passed\n";
	}

	# don't worry about rev-list on non-commit objects/tags,
	# it shouldn't blow up if a ref is a blob or tree...
	chomp(@_branch_from = `git-rev-parse --symbolic --all`);
}

# '<svn username> = real-name <email address>' mapping based on git-svnimport:
sub load_authors {
	open my $authors, '<', $_authors or die "Can't open $_authors $!\n";
	while (<$authors>) {
		chomp;
		next unless /^(\S+?)\s*=\s*(.+?)\s*<(.+)>\s*$/;
		my ($user, $name, $email) = ($1, $2, $3);
		$users{$user} = [$name, $email];
	}
	close $authors or croak $!;
}

sub rload_authors {
	open my $authors, '<', $_authors or die "Can't open $_authors $!\n";
	while (<$authors>) {
		chomp;
		next unless /^(\S+?)\s*=\s*(.+?)\s*<(.+)>\s*$/;
		my ($user, $name, $email) = ($1, $2, $3);
		$rusers{"$name <$email>"} = $user;
	}
	close $authors or croak $!;
}

sub svn_propget_base {
	my ($p, $f) = @_;
	$f .= '@BASE' if $_svn_pg_peg_revs;
	return safe_qx(qw/svn propget/, $p, $f);
}

sub git_svn_each {
	my $sub = shift;
	foreach (`git-rev-parse --symbolic --all`) {
		next unless s#^refs/remotes/##;
		chomp $_;
		next unless -f "$GIT_DIR/svn/$_/info/url";
		&$sub($_);
	}
}

sub migrate_revdb {
	git_svn_each(sub {
		my $id = shift;
		defined(my $pid = fork) or croak $!;
		if (!$pid) {
			$GIT_SVN = $ENV{GIT_SVN_ID} = $id;
			init_vars();
			exit 0 if -r $REVDB;
			print "Upgrading svn => git mapping...\n";
			open my $fh, '>>',$REVDB or croak $!;
			close $fh;
			rebuild();
			print "Done upgrading. You may now delete the ",
				"deprecated $GIT_SVN_DIR/revs directory\n";
			exit 0;
		}
		waitpid $pid, 0;
		croak $? if $?;
	});
}

sub migration_check {
	migrate_revdb() unless (-e $REVDB);
	return if (-d "$GIT_DIR/svn" || !-d $GIT_DIR);
	print "Upgrading repository...\n";
	unless (-d "$GIT_DIR/svn") {
		mkdir "$GIT_DIR/svn" or croak $!;
	}
	print "Data from a previous version of git-svn exists, but\n\t",
				"$GIT_SVN_DIR\n\t(required for this version ",
				"($VERSION) of git-svn) does not.\n";

	foreach my $x (`git-rev-parse --symbolic --all`) {
		next unless $x =~ s#^refs/remotes/##;
		chomp $x;
		next unless -f "$GIT_DIR/$x/info/url";
		my $u = eval { file_to_s("$GIT_DIR/$x/info/url") };
		next unless $u;
		my $dn = dirname("$GIT_DIR/svn/$x");
		mkpath([$dn]) unless -d $dn;
		rename "$GIT_DIR/$x", "$GIT_DIR/svn/$x" or croak "$!: $x";
		my ($url, $path) = repo_path_split($u);
		s_to_file($url, "$GIT_DIR/svn/$x/info/repo_url");
		s_to_file($path, "$GIT_DIR/svn/$x/info/repo_path");
	}
	migrate_revdb() if (-d $GIT_SVN_DIR && !-w $REVDB);
	print "Done upgrading.\n";
}

sub find_rev_before {
	my ($r, $id, $eq_ok) = @_;
	my $f = "$GIT_DIR/svn/$id/.rev_db";
	# --$r unless $eq_ok;
	while ($r > 0) {
		if (my $c = revdb_get($f, $r)) {
			return ($r, $c);
		}
		--$r;
	}
	return (undef, undef);
}

sub init_vars {
	$GIT_SVN ||= $ENV{GIT_SVN_ID} || 'git-svn';
	$GIT_SVN_DIR = "$GIT_DIR/svn/$GIT_SVN";
	$REVDB = "$GIT_SVN_DIR/.rev_db";
	$GIT_SVN_INDEX = "$GIT_SVN_DIR/index";
	$SVN_URL = undef;
	$SVN_WC = "$GIT_SVN_DIR/tree";
}

# convert GetOpt::Long specs for use by git-repo-config
sub read_repo_config {
	return unless -d $GIT_DIR;
	my $opts = shift;
	foreach my $o (keys %$opts) {
		my $v = $opts->{$o};
		my ($key) = ($o =~ /^([a-z\-]+)/);
		$key =~ s/-//g;
		my $arg = 'git-repo-config';
		$arg .= ' --int' if ($o =~ /[:=]i$/);
		$arg .= ' --bool' if ($o !~ /[:=][sfi]$/);
		if (ref $v eq 'ARRAY') {
			chomp(my @tmp = `$arg --get-all svn.$key`);
			@$v = @tmp if @tmp;
		} else {
			chomp(my $tmp = `$arg --get svn.$key`);
			if ($tmp && !($arg =~ / --bool / && $tmp eq 'false')) {
				$$v = $tmp;
			}
		}
	}
}

sub set_default_vals {
	if (defined $_repack) {
		$_repack = 1000 if ($_repack <= 0);
		$_repack_nr = $_repack;
		$_repack_flags ||= '';
	}
}

sub read_grafts {
	my $gr_file = shift;
	my ($grafts, $comments) = ({}, {});
	if (open my $fh, '<', $gr_file) {
		my @tmp;
		while (<$fh>) {
			if (/^($sha1)\s+/) {
				my $c = $1;
				if (@tmp) {
					@{$comments->{$c}} = @tmp;
					@tmp = ();
				}
				foreach my $p (split /\s+/, $_) {
					$grafts->{$c}->{$p} = 1;
				}
			} else {
				push @tmp, $_;
			}
		}
		close $fh or croak $!;
		@{$comments->{'END'}} = @tmp if @tmp;
	}
	return ($grafts, $comments);
}

sub write_grafts {
	my ($grafts, $comments, $gr_file) = @_;

	open my $fh, '>', $gr_file or croak $!;
	foreach my $c (sort keys %$grafts) {
		if ($comments->{$c}) {
			print $fh $_ foreach @{$comments->{$c}};
		}
		my $p = $grafts->{$c};
		delete $p->{$c}; # commits are not self-reproducing...
		my $pid = open my $ch, '-|';
		defined $pid or croak $!;
		if (!$pid) {
			exec(qw/git-cat-file commit/, $c) or croak $!;
		}
		while (<$ch>) {
			if (/^parent ([a-f\d]{40})/) {
				$p->{$1} = 1;
			} else {
				last unless /^\S/i;
			}
		}
		close $ch; # breaking the pipe
		print $fh $c, ' ', join(' ', sort keys %$p),"\n";
	}
	if ($comments->{'END'}) {
		print $fh $_ foreach @{$comments->{'END'}};
	}
	close $fh or croak $!;
}

sub read_url_paths {
	my $l_map = {};
	git_svn_each(sub { my $x = shift;
			my $u = file_to_s("$GIT_DIR/svn/$x/info/repo_url");
			my $p = file_to_s("$GIT_DIR/svn/$x/info/repo_path");
			# we hate trailing slashes
			if ($u =~ s#(?:^\/+|\/+$)##g) {
				s_to_file($u,"$GIT_DIR/svn/$x/info/repo_url");
			}
			if ($p =~ s#(?:^\/+|\/+$)##g) {
				s_to_file($p,"$GIT_DIR/svn/$x/info/repo_path");
			}
			$l_map->{$u}->{$p} = $x;
			});
	return $l_map;
}

sub extract_metadata {
	my $id = shift;
	my ($url, $rev, $uuid) = ($id =~ /^git-svn-id:\s(\S+?)\@(\d+)
							\s([a-f\d\-]+)$/x);
	if (!$rev || !$uuid || !$url) {
		# some of the original repositories I made had
		# indentifiers like this:
		($rev, $uuid) = ($id =~/^git-svn-id:\s(\d+)\@([a-f\d\-]+)/);
	}
	return ($url, $rev, $uuid);
}

sub tz_to_s_offset {
	my ($tz) = @_;
	$tz =~ s/(\d\d)$//;
	return ($1 * 60) + ($tz * 3600);
}

sub setup_pager { # translated to Perl from pager.c
	return unless (-t *STDOUT);
	my $pager = $ENV{PAGER};
	if (!defined $pager) {
		$pager = 'less';
	} elsif (length $pager == 0 || $pager eq 'cat') {
		return;
	}
	pipe my $rfd, my $wfd or return;
	defined(my $pid = fork) or croak $!;
	if (!$pid) {
		open STDOUT, '>&', $wfd or croak $!;
		return;
	}
	open STDIN, '<&', $rfd or croak $!;
	$ENV{LESS} ||= '-S';
	exec $pager or croak "Can't run pager: $!\n";;
}

sub get_author_info {
	my ($dest, $author, $t, $tz) = @_;
	$author =~ s/(?:^\s*|\s*$)//g;
	my $_a;
	if ($_authors) {
		$_a = $rusers{$author} || undef;
	}
	if (!$_a) {
		($_a) = ($author =~ /<([^>]+)\@[^>]+>$/);
	}
	$dest->{t} = $t;
	$dest->{tz} = $tz;
	$dest->{a} = $_a;
	# Date::Parse isn't in the standard Perl distro :(
	if ($tz =~ s/^\+//) {
		$t += tz_to_s_offset($tz);
	} elsif ($tz =~ s/^\-//) {
		$t -= tz_to_s_offset($tz);
	}
	$dest->{t_utc} = $t;
}

sub process_commit {
	my ($c, $r_min, $r_max, $defer) = @_;
	if (defined $r_min && defined $r_max) {
		if ($r_min == $c->{r} && $r_min == $r_max) {
			show_commit($c);
			return 0;
		}
		return 1 if $r_min == $r_max;
		if ($r_min < $r_max) {
			# we need to reverse the print order
			return 0 if (defined $_limit && --$_limit < 0);
			push @$defer, $c;
			return 1;
		}
		if ($r_min != $r_max) {
			return 1 if ($r_min < $c->{r});
			return 1 if ($r_max > $c->{r});
		}
	}
	return 0 if (defined $_limit && --$_limit < 0);
	show_commit($c);
	return 1;
}

sub show_commit {
	my $c = shift;
	if ($_oneline) {
		my $x = "\n";
		if (my $l = $c->{l}) {
			while ($l->[0] =~ /^\s*$/) { shift @$l }
			$x = $l->[0];
		}
		$_l_fmt ||= 'A' . length($c->{r});
		print 'r',pack($_l_fmt, $c->{r}),' | ';
		print "$c->{c} | " if $_show_commit;
		print $x;
	} else {
		show_commit_normal($c);
	}
}

sub show_commit_normal {
	my ($c) = @_;
	print '-' x72, "\nr$c->{r} | ";
	print "$c->{c} | " if $_show_commit;
	print "$c->{a} | ", strftime("%Y-%m-%d %H:%M:%S %z (%a, %d %b %Y)",
				 localtime($c->{t_utc})), ' | ';
	my $nr_line = 0;

	if (my $l = $c->{l}) {
		while ($l->[$#$l] eq "\n" && $l->[($#$l - 1)] eq "\n") {
			pop @$l;
		}
		$nr_line = scalar @$l;
		if (!$nr_line) {
			print "1 line\n\n\n";
		} else {
			if ($nr_line == 1) {
				$nr_line = '1 line';
			} else {
				$nr_line .= ' lines';
			}
			print $nr_line, "\n\n";
			print $_ foreach @$l;
		}
	} else {
		print "1 line\n\n";

	}
	foreach my $x (qw/raw diff/) {
		if ($c->{$x}) {
			print "\n";
			print $_ foreach @{$c->{$x}}
		}
	}
}

sub libsvn_load {
	return unless $_use_lib;
	$_use_lib = eval {
		require SVN::Core;
		if ($SVN::Core::VERSION lt '1.2.1') {
			die "Need SVN::Core 1.2.1 or better ",
					"(got $SVN::Core::VERSION) ",
					"Falling back to command-line svn\n";
		}
		require SVN::Ra;
		require SVN::Delta;
		push @SVN::Git::Editor::ISA, 'SVN::Delta::Editor';
		my $kill_stupid_warnings = $SVN::Node::none.$SVN::Node::file.
					$SVN::Node::dir.$SVN::Node::unknown.
					$SVN::Node::none.$SVN::Node::file.
					$SVN::Node::dir.$SVN::Node::unknown;
		1;
	};
}

sub libsvn_connect {
	my ($url) = @_;
	my $auth = SVN::Core::auth_open([SVN::Client::get_simple_provider(),
			  SVN::Client::get_ssl_server_trust_file_provider(),
			  SVN::Client::get_username_provider()]);
	my $s = eval { SVN::Ra->new(url => $url, auth => $auth) };
	return $s;
}

sub libsvn_get_file {
	my ($gui, $f, $rev) = @_;
	my $p = $f;
	return unless ($p =~ s#^\Q$SVN_PATH\E/?##);

	my $fd = IO::File->new_tmpfile or croak $!;
	my $pool = SVN::Pool->new;
	my ($r, $props) = $SVN->get_file($f, $rev, $fd, $pool);
	$pool->clear;
	$fd->flush == 0 or croak $!;
	seek $fd, 0, 0 or croak $!;
	if (my $es = $props->{'svn:eol-style'}) {
		my $new_fd = IO::File->new_tmpfile or croak $!;
		eol_cp_fd($fd, $new_fd, $es);
		close $fd or croak $!;
		$fd = $new_fd;
		seek $fd, 0, 0 or croak $!;
		$fd->flush == 0 or croak $!;
	}
	my $mode = '100644';
	if (exists $props->{'svn:executable'}) {
		$mode = '100755';
	}
	if (exists $props->{'svn:special'}) {
		$mode = '120000';
		local $/;
		my $link = <$fd>;
		$link =~ s/^link // or die "svn:special file with contents: <",
						$link, "> is not understood\n";
		seek $fd, 0, 0 or croak $!;
		truncate $fd, 0 or croak $!;
		print $fd $link or croak $!;
		seek $fd, 0, 0 or croak $!;
		$fd->flush == 0 or croak $!;
	}
	my $pid = open my $ho, '-|';
	defined $pid or croak $!;
	if (!$pid) {
		open STDIN, '<&', $fd or croak $!;
		exec qw/git-hash-object -w --stdin/ or croak $!;
	}
	chomp(my $hash = do { local $/; <$ho> });
	close $ho or croak $?;
	$hash =~ /^$sha1$/o or die "not a sha1: $hash\n";
	print $gui $mode,' ',$hash,"\t",$p,"\0" or croak $!;
	close $fd or croak $!;
}

sub libsvn_log_entry {
	my ($rev, $author, $date, $msg, $parents) = @_;
	my ($Y,$m,$d,$H,$M,$S) = ($date =~ /^(\d{4})\-(\d\d)\-(\d\d)T
					 (\d\d)\:(\d\d)\:(\d\d).\d+Z$/x)
				or die "Unable to parse date: $date\n";
	if (defined $_authors && ! defined $users{$author}) {
		die "Author: $author not defined in $_authors file\n";
	}
	return { revision => $rev, date => "+0000 $Y-$m-$d $H:$M:$S",
		author => $author, msg => $msg."\n", parents => $parents || [] }
}

sub process_rm {
	my ($gui, $last_commit, $f) = @_;
	$f =~ s#^\Q$SVN_PATH\E/?## or return;
	# remove entire directories.
	if (safe_qx('git-ls-tree',$last_commit,'--',$f) =~ /^040000 tree/) {
		defined(my $pid = open my $ls, '-|') or croak $!;
		if (!$pid) {
			exec(qw/git-ls-tree -r --name-only -z/,
				$last_commit,'--',$f) or croak $!;
		}
		local $/ = "\0";
		while (<$ls>) {
			print $gui '0 ',0 x 40,"\t",$_ or croak $!;
		}
		close $ls or croak $!;
	} else {
		print $gui '0 ',0 x 40,"\t",$f,"\0" or croak $!;
	}
}

sub libsvn_fetch {
	my ($last_commit, $paths, $rev, $author, $date, $msg) = @_;
	open my $gui, '| git-update-index -z --index-info' or croak $!;
	my @amr;
	foreach my $f (keys %$paths) {
		my $m = $paths->{$f}->action();
		$f =~ s#^/+##;
		if ($m =~ /^[DR]$/) {
			process_rm($gui, $last_commit, $f);
			next if $m eq 'D';
			# 'R' can be file replacements, too, right?
		}
		my $pool = SVN::Pool->new;
		my $t = $SVN->check_path($f, $rev, $pool);
		if ($t == $SVN::Node::file) {
			if ($m =~ /^[AMR]$/) {
				push @amr, $f;
			} else {
				die "Unrecognized action: $m, ($f r$rev)\n";
			}
		}
		$pool->clear;
	}
	libsvn_get_file($gui, $_, $rev) foreach (@amr);
	close $gui or croak $!;
	return libsvn_log_entry($rev, $author, $date, $msg, [$last_commit]);
}

sub svn_grab_base_rev {
	defined(my $pid = open my $fh, '-|') or croak $!;
	if (!$pid) {
		open my $null, '>', '/dev/null' or croak $!;
		open STDERR, '>&', $null or croak $!;
		exec qw/git-rev-parse --verify/,"refs/remotes/$GIT_SVN^0"
								or croak $!;
	}
	chomp(my $c = do { local $/; <$fh> });
	close $fh;
	if (defined $c && length $c) {
		my ($url, $rev, $uuid) = extract_metadata((grep(/^git-svn-id: /,
			safe_qx(qw/git-cat-file commit/, $c)))[0]);
		return ($rev, $c);
	}
	return (undef, undef);
}

sub libsvn_parse_revision {
	my $base = shift;
	my $head = $SVN->get_latest_revnum();
	if (!defined $_revision || $_revision eq 'BASE:HEAD') {
		return ($base + 1, $head) if (defined $base);
		return (0, $head);
	}
	return ($1, $2) if ($_revision =~ /^(\d+):(\d+)$/);
	return ($_revision, $_revision) if ($_revision =~ /^\d+$/);
	if ($_revision =~ /^BASE:(\d+)$/) {
		return ($base + 1, $1) if (defined $base);
		return (0, $head);
	}
	return ($1, $head) if ($_revision =~ /^(\d+):HEAD$/);
	die "revision argument: $_revision not understood by git-svn\n",
		"Try using the command-line svn client instead\n";
}

sub libsvn_traverse {
	my ($gui, $pfx, $path, $rev) = @_;
	my $cwd = "$pfx/$path";
	my $pool = SVN::Pool->new;
	$cwd =~ s#^/+##g;
	my ($dirent, $r, $props) = $SVN->get_dir($cwd, $rev, $pool);
	foreach my $d (keys %$dirent) {
		my $t = $dirent->{$d}->kind;
		if ($t == $SVN::Node::dir) {
			libsvn_traverse($gui, $cwd, $d, $rev);
		} elsif ($t == $SVN::Node::file) {
			libsvn_get_file($gui, "$cwd/$d", $rev);
		}
	}
	$pool->clear;
}

sub libsvn_traverse_ignore {
	my ($fh, $path, $r) = @_;
	$path =~ s#^/+##g;
	my $pool = SVN::Pool->new;
	my ($dirent, undef, $props) = $SVN->get_dir($path, $r, $pool);
	my $p = $path;
	$p =~ s#^\Q$SVN_PATH\E/?##;
	print $fh length $p ? "\n# $p\n" : "\n# /\n";
	if (my $s = $props->{'svn:ignore'}) {
		$s =~ s/[\r\n]+/\n/g;
		chomp $s;
		if (length $p == 0) {
			$s =~ s#\n#\n/$p#g;
			print $fh "/$s\n";
		} else {
			$s =~ s#\n#\n/$p/#g;
			print $fh "/$p/$s\n";
		}
	}
	foreach (sort keys %$dirent) {
		next if $dirent->{$_}->kind != $SVN::Node::dir;
		libsvn_traverse_ignore($fh, "$path/$_", $r);
	}
	$pool->clear;
}

sub revisions_eq {
	my ($path, $r0, $r1) = @_;
	return 1 if $r0 == $r1;
	my $nr = 0;
	if ($_use_lib) {
		# should be OK to use Pool here (r1 - r0) should be small
		my $pool = SVN::Pool->new;
		$SVN->get_log("/$path", $r0, $r1, 0, 1, 1, sub {$nr++},$pool);
		$pool->clear;
	} else {
		my ($url, undef) = repo_path_split($SVN_URL);
		my $svn_log = svn_log_raw("$url/$path","-r$r0:$r1");
		while (next_log_entry($svn_log)) { $nr++ }
		close $svn_log->{fh};
	}
	return 0 if ($nr > 1);
	return 1;
}

sub libsvn_find_parent_branch {
	return undef; # XXX this function is disabled atm (not tested enough)
	my ($paths, $rev, $author, $date, $msg) = @_;
	my $svn_path = '/'.$SVN_PATH;

	# look for a parent from another branch:
	foreach (keys %$paths) {
		next if ($_ ne $svn_path);
		my $i = $paths->{$_};
		my $branch_from = $i->copyfrom_path or next;
		my $r = $i->copyfrom_rev;
		print STDERR  "Found possible branch point: ",
					"$branch_from => $svn_path, $r\n";
		$branch_from =~ s#^/##;
		my $l_map = read_url_paths();
		my $url = $SVN->{url};
		defined $l_map->{$url} or next;
		my $id  = $l_map->{$url}->{$branch_from} or next;
		my ($r0, $parent) = find_rev_before($r,$id,1);
		if (defined $r0 && defined $parent &&
					revisions_eq($branch_from, $r0, $r)) {
			unlink $GIT_SVN_INDEX;
			print STDERR "Found branch parent: $parent\n";
			sys(qw/git-read-tree/, $parent);
			return libsvn_fetch($parent, $paths, $rev,
						$author, $date, $msg);
		} else {
			print STDERR
				"Nope, branch point not imported or unknown\n";
		}
	}
	return undef;
}

sub libsvn_new_tree {
	if (my $log_entry = libsvn_find_parent_branch(@_)) {
		return $log_entry;
	}
	my ($paths, $rev, $author, $date, $msg) = @_;
	open my $gui, '| git-update-index -z --index-info' or croak $!;
	my $pool = SVN::Pool->new;
	libsvn_traverse($gui, '', $SVN_PATH, $rev, $pool);
	$pool->clear;
	close $gui or croak $!;
	return libsvn_log_entry($rev, $author, $date, $msg);
}

sub find_graft_path_commit {
	my ($tree_paths, $p1, $r1) = @_;
	foreach my $x (keys %$tree_paths) {
		next unless ($p1 =~ /^\Q$x\E/);
		my $i = $tree_paths->{$x};
		my ($r0, $parent) = find_rev_before($r1,$i,1);
		return $parent if (defined $r0 && $r0 == $r1);
		print STDERR "r$r1 of $i not imported\n";
		next;
	}
	return undef;
}

sub find_graft_path_parents {
	my ($grafts, $tree_paths, $c, $p0, $r0) = @_;
	foreach my $x (keys %$tree_paths) {
		next unless ($p0 =~ /^\Q$x\E/);
		my $i = $tree_paths->{$x};
		my ($r, $parent) = find_rev_before($r0, $i, 1);
		if (defined $r && defined $parent && revisions_eq($x,$r,$r0)) {
			$grafts->{$c}->{$parent} = 1;
		}
	}
}

sub libsvn_graft_file_copies {
	my ($grafts, $tree_paths, $path, $paths, $rev) = @_;
	foreach (keys %$paths) {
		my $i = $paths->{$_};
		my ($m, $p0, $r0) = ($i->action, $i->copyfrom_path,
					$i->copyfrom_rev);
		next unless (defined $p0 && defined $r0);

		my $p1 = $_;
		$p1 =~ s#^/##;
		$p0 =~ s#^/##;
		my $c = find_graft_path_commit($tree_paths, $p1, $rev);
		next unless $c;
		find_graft_path_parents($grafts, $tree_paths, $c, $p0, $r0);
	}
}

sub set_index {
	my $old = $ENV{GIT_INDEX_FILE};
	$ENV{GIT_INDEX_FILE} = shift;
	return $old;
}

sub restore_index {
	my ($old) = @_;
	if (defined $old) {
		$ENV{GIT_INDEX_FILE} = $old;
	} else {
		delete $ENV{GIT_INDEX_FILE};
	}
}

sub libsvn_commit_cb {
	my ($rev, $date, $committer, $c, $msg, $r_last, $cmt_last) = @_;
	if ($_optimize_commits && $rev == ($r_last + 1)) {
		my $log = libsvn_log_entry($rev,$committer,$date,$msg);
		$log->{tree} = get_tree_from_treeish($c);
		my $cmt = git_commit($log, $cmt_last, $c);
		my @diff = safe_qx('git-diff-tree', $cmt, $c);
		if (@diff) {
			print STDERR "Trees differ: $cmt $c\n",
					join('',@diff),"\n";
			exit 1;
		}
	} else {
		fetch_lib("$rev=$c");
	}
}

sub libsvn_ls_fullurl {
	my $fullurl = shift;
	my ($repo, $path) = repo_path_split($fullurl);
	$SVN ||= libsvn_connect($repo);
	my @ret;
	my $pool = SVN::Pool->new;
	my ($dirent, undef, undef) = $SVN->get_dir($path,
						$SVN->get_latest_revnum, $pool);
	foreach my $d (keys %$dirent) {
		if ($dirent->{$d}->kind == $SVN::Node::dir) {
			push @ret, "$d/"; # add '/' for compat with cli svn
		}
	}
	$pool->clear;
	return @ret;
}


sub libsvn_skip_unknown_revs {
	my $err = shift;
	my $errno = $err->apr_err();
	# Maybe the branch we're tracking didn't
	# exist when the repo started, so it's
	# not an error if it doesn't, just continue
	#
	# Wonderfully consistent library, eh?
	# 160013 - svn:// and file://
	# 175002 - http(s)://
	#   More codes may be discovered later...
	if ($errno == 175002 || $errno == 160013) {
		print STDERR "directory non-existent\n";
		return;
	}
	croak "Error from SVN, ($errno): ", $err->expanded_message,"\n";
};

# Tie::File seems to be prone to offset errors if revisions get sparse,
# it's not that fast, either.  Tie::File is also not in Perl 5.6.  So
# one of my favorite modules is out :<  Next up would be one of the DBM
# modules, but I'm not sure which is most portable...  So I'll just
# go with something that's plain-text, but still capable of
# being randomly accessed.  So here's my ultra-simple fixed-width
# database.  All records are 40 characters + "\n", so it's easy to seek
# to a revision: (41 * rev) is the byte offset.
# A record of 40 0s denotes an empty revision.
# And yes, it's still pretty fast (faster than Tie::File).
sub revdb_set {
	my ($file, $rev, $commit) = @_;
	length $commit == 40 or croak "arg3 must be a full SHA1 hexsum\n";
	open my $fh, '+<', $file or croak $!;
	my $offset = $rev * 41;
	# assume that append is the common case:
	seek $fh, 0, 2 or croak $!;
	my $pos = tell $fh;
	if ($pos < $offset) {
		print $fh (('0' x 40),"\n") x (($offset - $pos) / 41);
	}
	seek $fh, $offset, 0 or croak $!;
	print $fh $commit,"\n";
	close $fh or croak $!;
}

sub revdb_get {
	my ($file, $rev) = @_;
	my $ret;
	my $offset = $rev * 41;
	open my $fh, '<', $file or croak $!;
	seek $fh, $offset, 0;
	if (tell $fh == $offset) {
		$ret = readline $fh;
		if (defined $ret) {
			chomp $ret;
			$ret = undef if ($ret =~ /^0{40}$/);
		}
	}
	close $fh or croak $!;
	return $ret;
}

package SVN::Git::Editor;
use vars qw/@ISA/;
use strict;
use warnings;
use Carp qw/croak/;
use IO::File;

sub new {
	my $class = shift;
	my $git_svn = shift;
	my $self = SVN::Delta::Editor->new(@_);
	bless $self, $class;
	foreach (qw/svn_path c r ra /) {
		die "$_ required!\n" unless (defined $git_svn->{$_});
		$self->{$_} = $git_svn->{$_};
	}
	$self->{pool} = SVN::Pool->new;
	$self->{bat} = { '' => $self->open_root($self->{r}, $self->{pool}) };
	$self->{rm} = { };
	require Digest::MD5;
	return $self;
}

sub split_path {
	return ($_[0] =~ m#^(.*?)/?([^/]+)$#);
}

sub repo_path {
	(defined $_[1] && length $_[1]) ? "$_[0]->{svn_path}/$_[1]"
					: $_[0]->{svn_path}
}

sub url_path {
	my ($self, $path) = @_;
	$self->{ra}->{url} . '/' . $self->repo_path($path);
}

sub rmdirs {
	my ($self) = @_;
	my $rm = $self->{rm};
	delete $rm->{''}; # we never delete the url we're tracking
	return unless %$rm;

	foreach (keys %$rm) {
		my @d = split m#/#, $_;
		my $c = shift @d;
		$rm->{$c} = 1;
		while (@d) {
			$c .= '/' . shift @d;
			$rm->{$c} = 1;
		}
	}
	delete $rm->{$self->{svn_path}};
	delete $rm->{''}; # we never delete the url we're tracking
	return unless %$rm;

	defined(my $pid = open my $fh,'-|') or croak $!;
	if (!$pid) {
		exec qw/git-ls-tree --name-only -r -z/, $self->{c} or croak $!;
	}
	local $/ = "\0";
	while (<$fh>) {
		chomp;
		$_ = $self->{svn_path} . '/' . $_;
		my ($dn) = ($_ =~ m#^(.*?)/?(?:[^/]+)$#);
		delete $rm->{$dn};
		last unless %$rm;
	}
	my ($r, $p, $bat) = ($self->{r}, $self->{pool}, $self->{bat});
	foreach my $d (sort { $b =~ tr#/#/# <=> $a =~ tr#/#/# } keys %$rm) {
		$self->close_directory($bat->{$d}, $p);
		my ($dn) = ($d =~ m#^(.*?)/?(?:[^/]+)$#);
		$self->SUPER::delete_entry($d, $r, $bat->{$dn}, $p);
		delete $bat->{$d};
	}
}

sub open_or_add_dir {
	my ($self, $full_path, $baton) = @_;
	my $p = SVN::Pool->new;
	my $t = $self->{ra}->check_path($full_path, $self->{r}, $p);
	$p->clear;
	if ($t == $SVN::Node::none) {
		return $self->add_directory($full_path, $baton,
						undef, -1, $self->{pool});
	} elsif ($t == $SVN::Node::dir) {
		return $self->open_directory($full_path, $baton,
						$self->{r}, $self->{pool});
	}
	print STDERR "$full_path already exists in repository at ",
		"r$self->{r} and it is not a directory (",
		($t == $SVN::Node::file ? 'file' : 'unknown'),"/$t)\n";
	exit 1;
}

sub ensure_path {
	my ($self, $path) = @_;
	my $bat = $self->{bat};
	$path = $self->repo_path($path);
	return $bat->{''} unless (length $path);
	my @p = split m#/+#, $path;
	my $c = shift @p;
	$bat->{$c} ||= $self->open_or_add_dir($c, $bat->{''});
	while (@p) {
		my $c0 = $c;
		$c .= '/' . shift @p;
		$bat->{$c} ||= $self->open_or_add_dir($c, $bat->{$c0});
	}
	return $bat->{$c};
}

sub A {
	my ($self, $m) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
					undef, -1);
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});
}

sub C {
	my ($self, $m) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
				$self->url_path($m->{file_a}), $self->{r});
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});
}

sub delete_entry {
	my ($self, $path, $pbat) = @_;
	my $rpath = $self->repo_path($path);
	my ($dir, $file) = split_path($rpath);
	$self->{rm}->{$dir} = 1;
	$self->SUPER::delete_entry($rpath, $self->{r}, $pbat, $self->{pool});
}

sub R {
	my ($self, $m) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
				$self->url_path($m->{file_a}), $self->{r});
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});

	($dir, $file) = split_path($m->{file_a});
	$pbat = $self->ensure_path($dir);
	$self->delete_entry($m->{file_a}, $pbat);
}

sub M {
	my ($self, $m) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->open_file($self->repo_path($m->{file_b}),
				$pbat,$self->{r},$self->{pool});
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});
}

sub T { shift->M(@_) }

sub change_file_prop {
	my ($self, $fbat, $pname, $pval) = @_;
	$self->SUPER::change_file_prop($fbat, $pname, $pval, $self->{pool});
}

sub chg_file {
	my ($self, $fbat, $m) = @_;
	if ($m->{mode_b} =~ /755$/ && $m->{mode_a} !~ /755$/) {
		$self->change_file_prop($fbat,'svn:executable','*');
	} elsif ($m->{mode_b} !~ /755$/ && $m->{mode_a} =~ /755$/) {
		$self->change_file_prop($fbat,'svn:executable',undef);
	}
	my $fh = IO::File->new_tmpfile or croak $!;
	if ($m->{mode_b} =~ /^120/) {
		print $fh 'link ' or croak $!;
		$self->change_file_prop($fbat,'svn:special','*');
	} elsif ($m->{mode_a} =~ /^120/ && $m->{mode_b} !~ /^120/) {
		$self->change_file_prop($fbat,'svn:special',undef);
	}
	defined(my $pid = fork) or croak $!;
	if (!$pid) {
		open STDOUT, '>&', $fh or croak $!;
		exec qw/git-cat-file blob/, $m->{sha1_b} or croak $!;
	}
	waitpid $pid, 0;
	croak $? if $?;
	$fh->flush == 0 or croak $!;
	seek $fh, 0, 0 or croak $!;

	my $md5 = Digest::MD5->new;
	$md5->addfile($fh) or croak $!;
	seek $fh, 0, 0 or croak $!;

	my $exp = $md5->hexdigest;
	my $atd = $self->apply_textdelta($fbat, undef, $self->{pool});
	my $got = SVN::TxDelta::send_stream($fh, @$atd, $self->{pool});
	die "Checksum mismatch\nexpected: $exp\ngot: $got\n" if ($got ne $exp);

	close $fh or croak $!;
}

sub D {
	my ($self, $m) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	$self->delete_entry($m->{file_b}, $pbat);
}

sub close_edit {
	my ($self) = @_;
	my ($p,$bat) = ($self->{pool}, $self->{bat});
	foreach (sort { $b =~ tr#/#/# <=> $a =~ tr#/#/# } keys %$bat) {
		$self->close_directory($bat->{$_}, $p);
	}
	$self->SUPER::close_edit($p);
	$p->clear;
}

sub abort_edit {
	my ($self) = @_;
	$self->SUPER::abort_edit($self->{pool});
	$self->{pool}->clear;
}

__END__

Data structures:

$svn_log hashref (as returned by svn_log_raw)
{
	fh => file handle of the log file,
	state => state of the log file parser (sep/msg/rev/msg_start...)
}

$log_msg hashref as returned by next_log_entry($svn_log)
{
	msg => 'whitespace-formatted log entry
',						# trailing newline is preserved
	revision => '8',			# integer
	date => '2004-02-24T17:01:44.108345Z',	# commit date
	author => 'committer name'
};


@mods = array of diff-index line hashes, each element represents one line
	of diff-index output

diff-index line ($m hash)
{
	mode_a => first column of diff-index output, no leading ':',
	mode_b => second column of diff-index output,
	sha1_b => sha1sum of the final blob,
	chg => change type [MCRADT],
	file_a => original file name of a file (iff chg is 'C' or 'R')
	file_b => new/current file name of a file (any chg)
}
;

Notes:
	I don't trust the each() function on unless I created %hash myself
	because the internal iterator may not have started at base.
