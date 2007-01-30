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
$VERSION = '@@GIT_VERSION@@';

use Cwd qw/abs_path/;
$GIT_DIR = abs_path($ENV{GIT_DIR} || '.git');
$ENV{GIT_DIR} = $GIT_DIR;

my $LC_ALL = $ENV{LC_ALL};
my $TZ = $ENV{TZ};
# make sure the svn binary gives consistent output between locales and TZs:
$ENV{TZ} = 'UTC';
$ENV{LC_ALL} = 'C';
$| = 1; # unbuffer STDOUT

# properties that we do not log:
my %SKIP = ( 'svn:wc:ra_dav:version-url' => 1,
             'svn:special' => 1,
             'svn:executable' => 1,
             'svn:entry:committed-rev' => 1,
             'svn:entry:last-author' => 1,
             'svn:entry:uuid' => 1,
             'svn:entry:committed-date' => 1,
);

sub fatal (@) { print STDERR @_; exit 1 }
require SVN::Core; # use()-ing this causes segfaults for me... *shrug*
require SVN::Ra;
require SVN::Delta;
if ($SVN::Core::VERSION lt '1.1.0') {
	fatal "Need SVN::Core 1.1.0 or better (got $SVN::Core::VERSION)\n";
}
push @SVN::Git::Editor::ISA, 'SVN::Delta::Editor';
push @SVN::Git::Fetcher::ISA, 'SVN::Delta::Editor';
*SVN::Git::Fetcher::process_rm = *process_rm;
use Carp qw/croak/;
use IO::File qw//;
use File::Basename qw/dirname basename/;
use File::Path qw/mkpath/;
use Getopt::Long qw/:config gnu_getopt no_ignore_case auto_abbrev pass_through/;
use POSIX qw/strftime/;
use IPC::Open3;
use Memoize;
use Git qw/command command_oneline command_noisy
           command_output_pipe command_input_pipe command_close_pipe/;
memoize('revisions_eq');
memoize('cmt_metadata');
memoize('get_commit_time');

my ($SVN);

my $_optimize_commits = 1 unless $ENV{GIT_SVN_NO_OPTIMIZE_COMMITS};
my $sha1 = qr/[a-f\d]{40}/;
my $sha1_short = qr/[a-f\d]{4,40}/;
my $_esc_color = qr/(?:\033\[(?:(?:\d+;)*\d*)?m)*/;
my ($_revision,$_stdin,$_no_ignore_ext,$_no_stop_copy,$_help,$_rmdir,$_edit,
	$_find_copies_harder, $_l, $_cp_similarity, $_cp_remote,
	$_repack, $_repack_nr, $_repack_flags, $_q,
	$_message, $_file, $_follow_parent, $_no_metadata,
	$_template, $_shared, $_no_default_regex, $_no_graft_copy,
	$_limit, $_verbose, $_incremental, $_oneline, $_l_fmt, $_show_commit,
	$_version, $_upgrade, $_authors, $_branch_all_refs, @_opt_m,
	$_merge, $_strategy, $_dry_run, $_ignore_nodate, $_non_recursive,
	$_username, $_config_dir, $_no_auth_cache,
	$_pager, $_color, $_prefix);
my (@_branch_from, %tree_map, %users, %rusers, %equiv);
my ($_svn_can_do_switch);
my @repo_path_split_cache;

my %fc_opts = ( 'no-ignore-externals' => \$_no_ignore_ext,
		'branch|b=s' => \@_branch_from,
		'follow-parent|follow' => \$_follow_parent,
		'branch-all-refs|B' => \$_branch_all_refs,
		'authors-file|A=s' => \$_authors,
		'repack:i' => \$_repack,
		'no-metadata' => \$_no_metadata,
		'quiet|q' => \$_q,
		'username=s' => \$_username,
		'config-dir=s' => \$_config_dir,
		'no-auth-cache' => \$_no_auth_cache,
		'ignore-nodate' => \$_ignore_nodate,
		'repack-flags|repack-args|repack-opts=s' => \$_repack_flags);

my ($_trunk, $_tags, $_branches);
my %multi_opts = ( 'trunk|T=s' => \$_trunk,
		'tags|t=s' => \$_tags,
		'branches|b=s' => \$_branches );
my %init_opts = ( 'template=s' => \$_template, 'shared' => \$_shared );
my %cmt_opts = ( 'edit|e' => \$_edit,
		'rmdir' => \$_rmdir,
		'find-copies-harder' => \$_find_copies_harder,
		'l=i' => \$_l,
		'copy-similarity|C=i'=> \$_cp_similarity
);

my %cmd = (
	fetch => [ \&cmd_fetch, "Download new revisions from SVN",
			{ 'revision|r=s' => \$_revision, %fc_opts } ],
	init => [ \&init, "Initialize a repo for tracking" .
			  " (requires URL argument)",
			  \%init_opts ],
	dcommit => [ \&dcommit, 'Commit several diffs to merge with upstream',
			{ 'merge|m|M' => \$_merge,
			  'strategy|s=s' => \$_strategy,
			  'dry-run|n' => \$_dry_run,
			%cmt_opts, %fc_opts } ],
	'set-tree' => [ \&commit, "Set an SVN repository to a git tree-ish",
			{	'stdin|' => \$_stdin, %cmt_opts, %fc_opts, } ],
	'show-ignore' => [ \&show_ignore, "Show svn:ignore listings",
			{ 'revision|r=i' => \$_revision } ],
	rebuild => [ \&rebuild, "Rebuild git-svn metadata (after git clone)",
			{ 'no-ignore-externals' => \$_no_ignore_ext,
			  'copy-remote|remote=s' => \$_cp_remote,
			  'upgrade' => \$_upgrade } ],
	'graft-branches' => [ \&graft_branches,
			'Detect merges/branches from already imported history',
			{ 'merge-rx|m' => \@_opt_m,
			  'branch|b=s' => \@_branch_from,
			  'branch-all-refs|B' => \$_branch_all_refs,
			  'no-default-regex' => \$_no_default_regex,
			  'no-graft-copy' => \$_no_graft_copy } ],
	'multi-init' => [ \&multi_init,
			'Initialize multiple trees (like git-svnimport)',
			{ %multi_opts, %init_opts,
			 'revision|r=i' => \$_revision,
			 'username=s' => \$_username,
			 'config-dir=s' => \$_config_dir,
			 'no-auth-cache' => \$_no_auth_cache,
			 'prefix=s' => \$_prefix,
			} ],
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
			  'non-recursive' => \$_non_recursive,
			  'authors-file|A=s' => \$_authors,
			  'color' => \$_color,
			  'pager=s' => \$_pager,
			} ],
	'commit-diff' => [ \&commit_diff, 'Commit a diff between two trees',
			{ 'message|m=s' => \$_message,
			  'file|F=s' => \$_file,
			  'revision|r=s' => \$_revision,
			%cmt_opts } ],
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
migration_check() unless $cmd =~ /^(?:init|rebuild|multi-init|commit-diff)$/;
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
		print $fd '  ',pack('A17',$_),$cmd{$_}->[1],"\n";
		foreach (keys %{$cmd{$_}->[2]}) {
			# prints out arguments as they should be passed:
			my $x = s#[:=]s$## ? '<arg>' : s#[:=]i$## ? '<num>' : '';
			print $fd ' ' x 21, join(', ', map { length $_ > 1 ?
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
	print "git-svn version $VERSION (svn $SVN::Core::VERSION)\n";
	exit 0;
}

sub rebuild {
	if (!verify_ref("refs/remotes/$GIT_SVN^0")) {
		copy_remote_ref();
	}
	$SVN_URL = shift or undef;
	my $newest_rev = 0;
	if ($_upgrade) {
		command_noisy('update-ref',"refs/remotes/$GIT_SVN","
		              $GIT_SVN-HEAD");
	} else {
		check_upgrade_needed();
	}

	my ($rev_list, $ctx) = command_output_pipe("rev-list",
	                                           "refs/remotes/$GIT_SVN");
	my $latest;
	while (<$rev_list>) {
		chomp;
		my $c = $_;
		croak "Non-SHA1: $c\n" unless $c =~ /^$sha1$/o;
		my @commit = grep(/^git-svn-id: /,
		                  command(qw/cat-file commit/, $c));
		next if (!@commit); # skip merges
		my ($url, $rev, $uuid) = extract_metadata($commit[$#commit]);
		if (!defined $rev || !$uuid) {
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
	command_close_pipe($rev_list, $ctx);
}

sub init {
	my $url = shift or die "SVN repository location required " .
				"as a command-line argument\n";
	$url =~ s!/+$!!; # strip trailing slash

	if (my $repo_path = shift) {
		unless (-d $repo_path) {
			mkpath([$repo_path]);
		}
		$GIT_DIR = $ENV{GIT_DIR} = $repo_path . "/.git";
		init_vars();
	}

	$SVN_URL = $url;
	unless (-d $GIT_DIR) {
		my @init_db = ('init');
		push @init_db, "--template=$_template" if defined $_template;
		push @init_db, "--shared" if defined $_shared;
		command_noisy(@init_db);
	}
	setup_git_svn();
}

sub cmd_fetch {
	fetch_child_id($GIT_SVN, @_);
}

sub fetch {
	check_upgrade_needed();
	$SVN_URL ||= file_to_s("$GIT_SVN_DIR/info/url");
	my $ret = fetch_lib(@_);
	if ($ret->{commit} && !verify_ref('refs/heads/master^0')) {
		command_noisy(qw(update-ref refs/heads/master),$ret->{commit});
	}
	return $ret;
}

sub fetch_lib {
	my (@parents) = @_;
	$SVN_URL ||= file_to_s("$GIT_SVN_DIR/info/url");
	$SVN ||= libsvn_connect($SVN_URL);
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
			command_noisy('read-tree', $last_commit);
		}
		my $x = command_oneline('write-tree');
		my ($y) = (command(qw/cat-file commit/, $last_commit)
							=~ /^tree ($sha1)/m);
		if ($y ne $x) {
			unlink $GIT_SVN_INDEX or croak $!;
			command_noisy('read-tree', $last_commit);
		}
		$x = command_oneline('write-tree');
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

			# Yes I'm perfectly aware that the fourth argument
			# below is the limit revisions number.  Unfortunately
			# performance sucks with it enabled, so it's much
			# faster to fetch revision ranges instead of relying
			# on the limiter.
			libsvn_get_log(libsvn_dup_ra($SVN), [''],
					$min, $max, 0, 1, 1,
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
			exit 0;
		}
		waitpid $pid, 0;
		croak $? if $?;
		($last_rev, $last_commit) = svn_grab_base_rev();
		last if ($max >= $head);
		$min = $max + 1;
		$max += $inc;
		$max = $head if ($max > $head);
		$SVN = libsvn_connect($SVN_URL);
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
		my @tmp = command('rev-parse',$c);
		if (scalar @tmp == 1) {
			push @revs, $tmp[0];
		} elsif (scalar @tmp > 1) {
			push @revs, reverse(command('rev-list',@tmp));
		} else {
			die "Failed to rev-parse $c\n";
		}
	}
	commit_lib(@revs);
	print "Done committing ",scalar @revs," revisions to SVN\n";
}

sub commit_lib {
	my (@revs) = @_;
	my ($r_last, $cmt_last) = svn_grab_base_rev();
	defined $r_last or die "Must have an existing revision to commit\n";
	my $fetched = fetch();
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

	my $repo;
	set_svn_commit_env();
	foreach my $c (@revs) {
		my $log_msg = get_commit_message($c, $commit_msg);

		# fork for each commit because there's a memory leak I
		# can't track down... (it's probably in the SVN code)
		defined(my $pid = open my $fh, '-|') or croak $!;
		if (!$pid) {
			my $ed = SVN::Git::Editor->new(
					{	r => $r_last,
						ra => libsvn_dup_ra($SVN),
						c => $c,
						svn_path => $SVN->{svn_path},
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
		close $fh or exit 1;
		if (! defined $r_new && ! defined $cmt_new) {
			unless ($no) {
				die "Failed to parse revision information\n";
			}
		} else {
			($r_last, $cmt_last) = ($r_new, $cmt_new);
		}
	}
	$ENV{LC_ALL} = 'C';
	unlink $commit_msg;
}

sub dcommit {
	my $head = shift || 'HEAD';
	my $gs = "refs/remotes/$GIT_SVN";
	my @refs = command(qw/rev-list --no-merges/, "$gs..$head");
	my $last_rev;
	foreach my $d (reverse @refs) {
		if (!verify_ref("$d~1")) {
			die "Commit $d\n",
			    "has no parent commit, and therefore ",
			    "nothing to diff against.\n",
			    "You should be working from a repository ",
			    "originally created by git-svn\n";
		}
		unless (defined $last_rev) {
			(undef, $last_rev, undef) = cmt_metadata("$d~1");
			unless (defined $last_rev) {
				die "Unable to extract revision information ",
				    "from commit $d~1\n";
			}
		}
		if ($_dry_run) {
			print "diff-tree $d~1 $d\n";
		} else {
			if (my $r = commit_diff("$d~1", $d, undef, $last_rev)) {
				$last_rev = $r;
			} # else: no changes, same $last_rev
		}
	}
	return if $_dry_run;
	fetch();
	my @diff = command('diff-tree', 'HEAD', $gs, '--');
	my @finish;
	if (@diff) {
		@finish = qw/rebase/;
		push @finish, qw/--merge/ if $_merge;
		push @finish, "--strategy=$_strategy" if $_strategy;
		print STDERR "W: HEAD and $gs differ, using @finish:\n", @diff;
	} else {
		print "No changes between current HEAD and $gs\n",
		      "Resetting to the latest $gs\n";
		@finish = qw/reset --mixed/;
	}
	command_noisy(@finish, $gs);
}

sub show_ignore {
	$SVN_URL ||= file_to_s("$GIT_SVN_DIR/info/url");
	my $repo;
	$SVN ||= libsvn_connect($SVN_URL);
	my $r = defined $_revision ? $_revision : $SVN->get_latest_revnum;
	libsvn_traverse_ignore(\*STDOUT, '', $r);
}

sub graft_branches {
	my $gr_file = "$GIT_DIR/info/grafts";
	my ($grafts, $comments) = read_grafts($gr_file);
	my $gr_sha1;

	if (%$grafts) {
		# temporarily disable our grafts file to make this idempotent
		chomp($gr_sha1 = command(qw/hash-object -w/,$gr_file));
		rename $gr_file, "$gr_file~$gr_sha1" or croak $!;
	}

	my $l_map = read_url_paths();
	my @re = map { qr/$_/is } @_opt_m if @_opt_m;
	unless ($_no_default_regex) {
		push @re, (qr/\b(?:merge|merging|merged)\s+with\s+([\w\.\-]+)/i,
			qr/\b(?:merge|merging|merged)\s+([\w\.\-]+)/i,
			qr/\b(?:from|of)\s+([\w\.\-]+)/i );
	}
	foreach my $u (keys %$l_map) {
		if (@re) {
			foreach my $p (keys %{$l_map->{$u}}) {
				graft_merge_msg($grafts,$l_map,$u,$p,@re);
			}
		}
		unless ($_no_graft_copy) {
			graft_file_copy_lib($grafts,$l_map,$u);
		}
	}
	graft_tree_joins($grafts);

	write_grafts($grafts, $comments, $gr_file);
	unlink "$gr_file~$gr_sha1" if $gr_sha1;
}

sub multi_init {
	my $url = shift;
	unless (defined $_trunk || defined $_branches || defined $_tags) {
		usage(1);
	}
	if (defined $_trunk) {
		my $trunk_url = complete_svn_url($url, $_trunk);
		my $ch_id;
		if ($GIT_SVN eq 'git-svn') {
			$ch_id = 1;
			$GIT_SVN = $ENV{GIT_SVN_ID} = 'trunk';
		}
		init_vars();
		unless (-d $GIT_SVN_DIR) {
			if ($ch_id) {
				print "GIT_SVN_ID set to 'trunk' for ",
				      "$trunk_url ($_trunk)\n";
			}
			init($trunk_url);
			command_noisy('config', 'svn.trunk', $trunk_url);
		}
	}
	$_prefix = '' unless defined $_prefix;
	complete_url_ls_init($url, $_branches, '--branches/-b', $_prefix);
	complete_url_ls_init($url, $_tags, '--tags/-t', $_prefix . 'tags/');
}

sub multi_fetch {
	# try to do trunk first, since branches/tags
	# may be descended from it.
	if (-e "$GIT_DIR/svn/trunk/info/url") {
		fetch_child_id('trunk', @_);
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

	config_pager();
	@args = (git_svn_log_cmd($r_min, $r_max), @args);
	my $log = command_output_pipe(@args);
	run_pager();
	my (@k, $c, $d);

	while (<$log>) {
		if (/^${_esc_color}commit ($sha1_short)/o) {
			my $cmt = $1;
			if ($c && cmt_showable($c) && $c->{r} != $r_last) {
				$r_last = $c->{r};
				process_commit($c, $r_min, $r_max, \@k) or
								goto out;
			}
			$d = undef;
			$c = { c => $cmt };
		} elsif (/^${_esc_color}author (.+) (\d+) ([\-\+]?\d+)$/) {
			get_author_info($c, $1, $2, $3);
		} elsif (/^${_esc_color}(?:tree|parent|committer) /) {
			# ignore
		} elsif (/^${_esc_color}:\d{6} \d{6} $sha1_short/o) {
			push @{$c->{raw}}, $_;
		} elsif (/^${_esc_color}[ACRMDT]\t/) {
			# we could add $SVN->{svn_path} here, but that requires
			# remote access at the moment (repo_path_split)...
			s#^(${_esc_color})([ACRMDT])\t#$1   $2 #;
			push @{$c->{changed}}, $_;
		} elsif (/^${_esc_color}diff /) {
			$d = 1;
			push @{$c->{diff}}, $_;
		} elsif ($d) {
			push @{$c->{diff}}, $_;
		} elsif (/^${_esc_color}    (git-svn-id:.+)$/) {
			($c->{url}, $c->{r}, undef) = extract_metadata($1);
		} elsif (s/^${_esc_color}    //) {
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
	eval { command_close_pipe($log) };
	print '-' x72,"\n" unless $_incremental || $_oneline;
}

sub commit_diff_usage {
	print STDERR "Usage: $0 commit-diff <tree-ish> <tree-ish> [<URL>]\n";
	exit 1
}

sub commit_diff {
	my $ta = shift or commit_diff_usage();
	my $tb = shift or commit_diff_usage();
	if (!eval { $SVN_URL = shift || file_to_s("$GIT_SVN_DIR/info/url") }) {
		print STDERR "Needed URL or usable git-svn id command-line\n";
		commit_diff_usage();
	}
	my $r = shift;
	unless (defined $r) {
		if (defined $_revision) {
			$r = $_revision
		} else {
			die "-r|--revision is a required argument\n";
		}
	}
	if (defined $_message && defined $_file) {
		print STDERR "Both --message/-m and --file/-F specified ",
				"for the commit message.\n",
				"I have no idea what you mean\n";
		exit 1;
	}
	if (defined $_file) {
		$_message = file_to_s($_file);
	} else {
		$_message ||= get_commit_message($tb,
					"$GIT_DIR/.svn-commit.tmp.$$")->{msg};
	}
	$SVN ||= libsvn_connect($SVN_URL);
	if ($r eq 'HEAD') {
		$r = $SVN->get_latest_revnum;
	} elsif ($r !~ /^\d+$/) {
		die "revision argument: $r not understood by git-svn\n";
	}
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef, 0) : ();
	my $rev_committed;
	my $ed = SVN::Git::Editor->new({	r => $r,
						ra => libsvn_dup_ra($SVN),
						c => $tb,
						svn_path => $SVN->{svn_path}
					},
				$SVN->get_commit_editor($_message,
					sub {
						$rev_committed = $_[0];
						print "Committed $_[0]\n";
					}, @lock)
				);
	eval {
		my $mods = libsvn_checkout_tree($ta, $tb, $ed);
		if (@$mods == 0) {
			print "No changes\n$ta == $tb\n";
			$ed->abort_edit;
		} else {
			$ed->close_edit;
		}
	};
	fatal "$@\n" if $@;
	$_message = $_file = undef;
	return $rev_committed;
}

########################### utility functions #########################

sub cmt_showable {
	my ($c) = @_;
	return 1 if defined $c->{r};
	if ($c->{l} && $c->{l}->[-1] eq "...\n" &&
				$c->{a_raw} =~ /\@([a-f\d\-]+)>$/) {
		my @msg = command(qw/cat-file commit/, $c->{c});
		shift @msg while ($msg[0] ne "\n");
		shift @msg;
		@{$c->{l}} = grep !/^git-svn-id: /, @msg;

		(undef, $c->{r}, undef) = extract_metadata(
				(grep(/^git-svn-id: /, @msg))[-1]);
	}
	return defined $c->{r};
}

sub log_use_color {
	return 1 if $_color;
	my ($dc, $dcvar);
	$dcvar = 'color.diff';
	$dc = `git-config --get $dcvar`;
	if ($dc eq '') {
		# nothing at all; fallback to "diff.color"
		$dcvar = 'diff.color';
		$dc = `git-config --get $dcvar`;
	}
	chomp($dc);
	if ($dc eq 'auto') {
		my $pc;
		$pc = `git-config --get color.pager`;
		if ($pc eq '') {
			# does not have it -- fallback to pager.color
			$pc = `git-config --bool --get pager.color`;
		}
		else {
			$pc = `git-config --bool --get color.pager`;
			if ($?) {
				$pc = 'false';
			}
		}
		chomp($pc);
		if (-t *STDOUT || (defined $_pager && $pc eq 'true')) {
			return ($ENV{TERM} && $ENV{TERM} ne 'dumb');
		}
		return 0;
	}
	return 0 if $dc eq 'never';
	return 1 if $dc eq 'always';
	chomp($dc = `git-config --bool --get $dcvar`);
	return ($dc eq 'true');
}

sub git_svn_log_cmd {
	my ($r_min, $r_max) = @_;
	my @cmd = (qw/log --abbrev-commit --pretty=raw
			--default/, "refs/remotes/$GIT_SVN");
	push @cmd, '-r' unless $_non_recursive;
	push @cmd, qw/--raw --name-status/ if $_verbose;
	push @cmd, '--color' if log_use_color();
	return @cmd unless defined $r_max;
	if ($r_max == $r_min) {
		push @cmd, '--max-count=1';
		if (my $c = revdb_get($REVDB, $r_max)) {
			push @cmd, $c;
		}
	} else {
		my ($c_min, $c_max);
		$c_max = revdb_get($REVDB, $r_max);
		$c_min = revdb_get($REVDB, $r_min);
		if (defined $c_min && defined $c_max) {
			if ($r_max > $r_max) {
				push @cmd, "$c_min..$c_max";
			} else {
				push @cmd, "$c_max..$c_min";
			}
		} elsif ($r_max > $r_min) {
			push @cmd, $c_max;
		} else {
			push @cmd, $c_min;
		}
	}
	return @cmd;
}

sub fetch_child_id {
	my $id = shift;
	print "Fetching $id\n";
	my $ref = "$GIT_DIR/refs/remotes/$id";
	defined(my $pid = open my $fh, '-|') or croak $!;
	if (!$pid) {
		$GIT_SVN = $ENV{GIT_SVN_ID} = $id;
		init_vars();
		fetch(@_);
		exit 0;
	}
	while (<$fh>) {
		print $_;
		check_repack() if (/^r\d+ = $sha1/o);
	}
	close $fh or croak $?;
}

sub rec_fetch {
	my ($pfx, $p, @args) = @_;
	my @dir;
	foreach (sort <$p/*>) {
		if (-r "$_/info/url") {
			$pfx .= '/' if $pfx && $pfx !~ m!/$!;
			my $id = $pfx . basename $_;
			next if $id eq 'trunk';
			fetch_child_id($id, @args);
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

sub complete_svn_url {
	my ($url, $path) = @_;
	$path =~ s#/+$##;
	$url =~ s#/+$## if $url;
	if ($path !~ m#^[a-z\+]+://#) {
		$path = '/' . $path if ($path !~ m#^/#);
		if (!defined $url || $url !~ m#^[a-z\+]+://#) {
			fatal("E: '$path' is not a complete URL ",
			      "and a separate URL is not specified\n");
		}
		$path = $url . $path;
	}
	return $path;
}

sub complete_url_ls_init {
	my ($url, $path, $switch, $pfx) = @_;
	unless ($path) {
		print STDERR "W: $switch not specified\n";
		return;
	}
	my $full_url = complete_svn_url($url, $path);
	my @ls = libsvn_ls_fullurl($full_url);
	defined(my $pid = fork) or croak $!;
	if (!$pid) {
		foreach my $u (map { "$full_url/$_" } (grep m!/$!, @ls)) {
			$u =~ s#/+$##;
			if ($u !~ m!\Q$full_url\E/(.+)$!) {
				print STDERR "W: Unrecognized URL: $u\n";
				die "This should never happen\n";
			}
			# don't try to init already existing refs
			my $id = $pfx.$1;
			$GIT_SVN = $ENV{GIT_SVN_ID} = $id;
			init_vars();
			unless (-d $GIT_SVN_DIR) {
				print "init $u => $id\n";
				init($u);
			}
		}
		exit 0;
	}
	waitpid $pid, 0;
	croak $? if $?;
	my ($n) = ($switch =~ /^--(\w+)/);
	command_noisy('config', "svn.$n", $full_url);
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

# grafts set here are 'stronger' in that they're based on actual tree
# matches, and won't be deleted from merge-base checking in write_grafts()
sub graft_tree_joins {
	my $grafts = shift;
	map_tree_joins() if (@_branch_from && !%tree_map);
	return unless %tree_map;

	git_svn_each(sub {
		my $i = shift;
		my @args = (qw/rev-list --pretty=raw/, "refs/remotes/$i");
		my ($fh, $ctx) = command_output_pipe(@args);
		while (<$fh>) {
			next unless /^commit ($sha1)$/o;
			my $c = $1;
			my ($t) = (<$fh> =~ /^tree ($sha1)$/o);
			next unless $tree_map{$t};

			my $l;
			do {
				$l = readline $fh;
			} until ($l =~ /^committer (?:.+) (\d+) ([\-\+]?\d+)$/);

			my ($s, $tz) = ($1, $2);
			if ($tz =~ s/^\+//) {
				$s += tz_to_s_offset($tz);
			} elsif ($tz =~ s/^\-//) {
				$s -= tz_to_s_offset($tz);
			}

			my ($url_a, $r_a, $uuid_a) = cmt_metadata($c);

			foreach my $p (@{$tree_map{$t}}) {
				next if $p eq $c;
				my $mb = eval { command('merge-base', $c, $p) };
				next unless ($@ || $?);
				if (defined $r_a) {
					# see if SVN says it's a relative
					my ($url_b, $r_b, $uuid_b) =
							cmt_metadata($p);
					next if (defined $url_b &&
							defined $url_a &&
							($url_a eq $url_b) &&
							($uuid_a eq $uuid_b));
					if ($uuid_a eq $uuid_b) {
						if ($r_b < $r_a) {
							$grafts->{$c}->{$p} = 2;
							next;
						} elsif ($r_b > $r_a) {
							$grafts->{$p}->{$c} = 2;
							next;
						}
					}
				}
				my $ct = get_commit_time($p);
				if ($ct < $s) {
					$grafts->{$c}->{$p} = 2;
				} elsif ($ct > $s) {
					$grafts->{$p}->{$c} = 2;
				}
				# what should we do when $ct == $s ?
			}
		}
		command_close_pipe($fh, $ctx);
	});
}

sub graft_file_copy_lib {
	my ($grafts, $l_map, $u) = @_;
	my $tree_paths = $l_map->{$u};
	my $pfx = common_prefix([keys %$tree_paths]);
	my ($repo, $path) = repo_path_split($u.$pfx);
	$SVN = libsvn_connect($repo);

	my ($base, $head) = libsvn_parse_revision();
	my $inc = 1000;
	my ($min, $max) = ($base, $head < $base+$inc ? $head : $base+$inc);
	my $eh = $SVN::Error::handler;
	$SVN::Error::handler = \&libsvn_skip_unknown_revs;
	while (1) {
		my $pool = SVN::Pool->new;
		libsvn_get_log(libsvn_dup_ra($SVN), [$path],
		               $min, $max, 0, 2, 1,
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
				push @strong, $l_map->{$u}->{$_};
				last;
			}
		}
		last if @strong;
		$w = basename($w);
		$re = qr/\Q$w\E/i;
		foreach (keys %{$l_map->{$u}}) {
			if (/$re/) {
				push @strong, $l_map->{$u}->{$_};
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
		my ($r0, $s0) = find_rev_before($rev, $m, 1);
		$grafts->{$c->{c}}->{$s0} = 1 if defined $s0;
	}
}

sub graft_merge_msg {
	my ($grafts, $l_map, $u, $p, @re) = @_;

	my $x = $l_map->{$u}->{$p};
	my $rl = rev_list_raw("refs/remotes/$x");
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
	my $pool = SVN::Pool->new;
	$SVN_UUID = $SVN->get_uuid($pool);
	$pool->clear;
}

sub verify_ref {
	my ($ref) = @_;
	eval { command_oneline([ 'rev-parse', '--verify', $ref ],
	                       { STDERR => 0 }); };
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
	my $tmp = libsvn_connect($full_url);
	return ($tmp->{repos_root}, $tmp->{svn_path});
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

}

sub get_tree_from_treeish {
	my ($treeish) = @_;
	croak "Not a sha1: $treeish\n" unless $treeish =~ /^$sha1$/o;
	my $type = command_oneline(qw/cat-file -t/, $treeish);
	my $expected;
	while ($type eq 'tag') {
		($treeish, $type) = command(qw/cat-file tag/, $treeish);
	}
	if ($type eq 'commit') {
		$expected = (grep /^tree /, command(qw/cat-file commit/,
		                                    $treeish))[0];
		($expected) = ($expected =~ /^tree ($sha1)$/);
		die "Unable to get tree from $treeish\n" unless $expected;
	} elsif ($type eq 'tree') {
		$expected = $treeish;
	} else {
		die "$treeish is a $type, expected tree, tag or commit\n";
	}
	return $expected;
}

sub get_diff {
	my ($from, $treeish) = @_;
	print "diff-tree $from $treeish\n";
	my @diff_tree = qw(diff-tree -z -r);
	if ($_cp_similarity) {
		push @diff_tree, "-C$_cp_similarity";
	} else {
		push @diff_tree, '-C';
	}
	push @diff_tree, '--find-copies-harder' if $_find_copies_harder;
	push @diff_tree, "-l$_l" if defined $_l;
	push @diff_tree, $from, $treeish;
	my ($diff_fh, $ctx) = command_output_pipe(@diff_tree);
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
	command_close_pipe($diff_fh, $ctx);
	return \@mods;
}

sub libsvn_checkout_tree {
	my ($from, $treeish, $ed) = @_;
	my $mods = get_diff($from, $treeish);
	return $mods unless (scalar @$mods);
	my %o = ( D => 1, R => 0, C => -1, A => 3, M => 3, T => 3 );
	foreach my $m (sort { $o{$a->{chg}} <=> $o{$b->{chg}} } @$mods) {
		my $f = $m->{chg};
		if (defined $o{$f}) {
			$ed->$f($m, $_q);
		} else {
			croak "Invalid change type: $f\n";
		}
	}
	$ed->rmdirs($_q) if $_rmdir;
	return $mods;
}

sub get_commit_message {
	my ($commit, $commit_msg) = (@_);
	my %log_msg = ( msg => '' );
	open my $msg, '>', $commit_msg or croak $!;

	my $type = command_oneline(qw/cat-file -t/, $commit);
	if ($type eq 'commit' || $type eq 'tag') {
		my ($msg_fh, $ctx) = command_output_pipe('cat-file',
		                                         $type, $commit);
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
		command_close_pipe($msg_fh, $ctx);
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

sub set_svn_commit_env {
	if (defined $LC_ALL) {
		$ENV{LC_ALL} = $LC_ALL;
	} else {
		delete $ENV{LC_ALL};
	}
}

sub rev_list_raw {
	my ($fh, $c) = command_output_pipe(qw/rev-list --pretty=raw/, @_);
	return { fh => $fh, ctx => $c, t => { } };
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
	command_close_pipe($fh, $rl->{ctx});
	return ($x != $rl->{t}) ? $x : undef;
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
		$tree = command_oneline('write-tree');
		croak $? if $?;
		restore_index($index);
	}
	# just in case we clobber the existing ref, we still want that ref
	# as our parent:
	if (my $cur = verify_ref("refs/remotes/$GIT_SVN^0")) {
		chomp $cur;
		push @tmp_parents, $cur;
	}

	if (exists $tree_map{$tree}) {
		foreach my $p (@{$tree_map{$tree}}) {
			my $skip;
			foreach (@tmp_parents) {
				# see if a common parent is found
				my $mb = eval { command('merge-base', $_, $p) };
				next if ($@ || $?);
				$skip = 1;
				last;
			}
			next if $skip;
			my ($url_p, $r_p, $uuid_p) = cmt_metadata($p);
			next if (($SVN_UUID eq $uuid_p) &&
						($log_msg->{revision} > $r_p));
			next if (defined $url_p && defined $SVN_URL &&
						($SVN_UUID eq $uuid_p) &&
						($url_p eq $SVN_URL));
			push @tmp_parents, $p;
		}
	}
	foreach (@tmp_parents) {
		next if $seen_parent{$_};
		$seen_parent{$_} = 1;
		push @exec_parents, $_;
		# MAXPARENT is defined to 16 in commit-tree.c:
		last if @exec_parents > 16;
	}

	set_commit_env($log_msg);
	my @exec = ('git-commit-tree', $tree);
	push @exec, '-p', $_  foreach @exec_parents;
	defined(my $pid = open3(my $msg_fh, my $out_fh, '>&STDERR', @exec))
								or croak $!;
	print $msg_fh $log_msg->{msg} or croak $!;
	unless ($_no_metadata) {
		print $msg_fh "\ngit-svn-id: $SVN_URL\@$log_msg->{revision}",
					" $SVN_UUID\n" or croak $!;
	}
	$msg_fh->flush == 0 or croak $!;
	close $msg_fh or croak $!;
	chomp(my $commit = do { local $/; <$out_fh> });
	close $out_fh or croak $!;
	waitpid $pid, 0;
	croak $? if $?;
	if ($commit !~ /^$sha1$/o) {
		die "Failed to commit, invalid sha1: $commit\n";
	}
	command_noisy('update-ref',"refs/remotes/$GIT_SVN",$commit);
	revdb_set($REVDB, $log_msg->{revision}, $commit);

	# this output is read via pipe, do not change:
	print "r$log_msg->{revision} = $commit\n";
	return $commit;
}

sub check_repack {
	if ($_repack && (--$_repack_nr == 0)) {
		$_repack_nr = $_repack;
		# repack doesn't use any arguments with spaces in them, does it?
		command_noisy('repack', split(/\s+/, $_repack_flags));
	}
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

sub check_upgrade_needed {
	if (!-r $REVDB) {
		-d $GIT_SVN_DIR or mkpath([$GIT_SVN_DIR]);
		open my $fh, '>>',$REVDB or croak $!;
		close $fh;
	}
	return unless eval {
		command([qw/rev-parse --verify/,"$GIT_SVN-HEAD^0"],
		        {STDERR => 0});
	};
	my $head = eval { command('rev-parse',"refs/remotes/$GIT_SVN") };
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
		my $pipe = command_output_pipe(qw/rev-list
		                            --topo-order --pretty=raw/, $br);
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
		eval { command_close_pipe($pipe) };
	}
}

sub load_all_refs {
	if (@_branch_from) {
		print STDERR '--branch|-b parameters are ignored when ',
			"--branch-all-refs|-B is passed\n";
	}

	# don't worry about rev-list on non-commit objects/tags,
	# it shouldn't blow up if a ref is a blob or tree...
	@_branch_from = command(qw/rev-parse --symbolic --all/);
}

# '<svn username> = real-name <email address>' mapping based on git-svnimport:
sub load_authors {
	open my $authors, '<', $_authors or die "Can't open $_authors $!\n";
	while (<$authors>) {
		chomp;
		next unless /^(\S+?|\(no author\))\s*=\s*(.+?)\s*<(.+)>\s*$/;
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

sub git_svn_each {
	my $sub = shift;
	foreach (command(qw/rev-parse --symbolic --all/)) {
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
			-d $GIT_SVN_DIR or mkpath([$GIT_SVN_DIR]);
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

	foreach my $x (command(qw/rev-parse --symbolic --all/)) {
		next unless $x =~ s#^refs/remotes/##;
		chomp $x;
		next unless -f "$GIT_DIR/$x/info/url";
		my $u = eval { file_to_s("$GIT_DIR/$x/info/url") };
		next unless $u;
		my $dn = dirname("$GIT_DIR/svn/$x");
		mkpath([$dn]) unless -d $dn;
		rename "$GIT_DIR/$x", "$GIT_DIR/svn/$x" or croak "$!: $x";
	}
	migrate_revdb() if (-d $GIT_SVN_DIR && !-w $REVDB);
	print "Done upgrading.\n";
}

sub find_rev_before {
	my ($r, $id, $eq_ok) = @_;
	my $f = "$GIT_DIR/svn/$id/.rev_db";
	return (undef,undef) unless -r $f;
	--$r unless $eq_ok;
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
	%tree_map = ();
}

# convert GetOpt::Long specs for use by git-config
sub read_repo_config {
	return unless -d $GIT_DIR;
	my $opts = shift;
	foreach my $o (keys %$opts) {
		my $v = $opts->{$o};
		my ($key) = ($o =~ /^([a-z\-]+)/);
		$key =~ s/-//g;
		my $arg = 'git-config';
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
		$_repack_flags ||= '-d';
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
		my %x; # real parents
		delete $p->{$c}; # commits are not self-reproducing...
		my $ch = command_output_pipe(qw/cat-file commit/, $c);
		while (<$ch>) {
			if (/^parent ($sha1)/) {
				$x{$1} = $p->{$1} = 1;
			} else {
				last unless /^\S/;
			}
		}
		eval { command_close_pipe($ch) }; # breaking the pipe

		# if real parents are the only ones in the grafts, drop it
		next if join(' ',sort keys %$p) eq join(' ',sort keys %x);

		my (@ip, @jp, $mb);
		my %del = %x;
		@ip = @jp = keys %$p;
		foreach my $i (@ip) {
			next if $del{$i} || $p->{$i} == 2;
			foreach my $j (@jp) {
				next if $i eq $j || $del{$j} || $p->{$j} == 2;
				$mb = eval { command('merge-base', $i, $j) };
				next unless $mb;
				chomp $mb;
				next if $x{$mb};
				if ($mb eq $j) {
					delete $p->{$i};
					$del{$i} = 1;
				} elsif ($mb eq $i) {
					delete $p->{$j};
					$del{$j} = 1;
				}
			}
		}

		# if real parents are the only ones in the grafts, drop it
		next if join(' ',sort keys %$p) eq join(' ',sort keys %x);

		print $fh $c, ' ', join(' ', sort keys %$p),"\n";
	}
	if ($comments->{'END'}) {
		print $fh $_ foreach @{$comments->{'END'}};
	}
	close $fh or croak $!;
}

sub read_url_paths_all {
	my ($l_map, $pfx, $p) = @_;
	my @dir;
	foreach (<$p/*>) {
		if (-r "$_/info/url") {
			$pfx .= '/' if $pfx && $pfx !~ m!/$!;
			my $id = $pfx . basename $_;
			my $url = file_to_s("$_/info/url");
			my ($u, $p) = repo_path_split($url);
			$l_map->{$u}->{$p} = $id;
		} elsif (-d $_) {
			push @dir, $_;
		}
	}
	foreach (@dir) {
		my $x = $_;
		$x =~ s!^\Q$GIT_DIR\E/svn/!!o;
		read_url_paths_all($l_map, $x, $_);
	}
}

# this one only gets ids that have been imported, not new ones
sub read_url_paths {
	my $l_map = {};
	git_svn_each(sub { my $x = shift;
			my $url = file_to_s("$GIT_DIR/svn/$x/info/url");
			my ($u, $p) = repo_path_split($url);
			$l_map->{$u}->{$p} = $x;
			});
	return $l_map;
}

sub extract_metadata {
	my $id = shift or return (undef, undef, undef);
	my ($url, $rev, $uuid) = ($id =~ /^git-svn-id:\s(\S+?)\@(\d+)
							\s([a-f\d\-]+)$/x);
	if (!defined $rev || !$uuid || !$url) {
		# some of the original repositories I made had
		# identifiers like this:
		($rev, $uuid) = ($id =~/^git-svn-id:\s(\d+)\@([a-f\d\-]+)/);
	}
	return ($url, $rev, $uuid);
}

sub cmt_metadata {
	return extract_metadata((grep(/^git-svn-id: /,
		command(qw/cat-file commit/, shift)))[-1]);
}

sub get_commit_time {
	my $cmt = shift;
	my $fh = command_output_pipe(qw/rev-list --pretty=raw -n1/, $cmt);
	while (<$fh>) {
		/^committer\s(?:.+) (\d+) ([\-\+]?\d+)$/ or next;
		my ($s, $tz) = ($1, $2);
		if ($tz =~ s/^\+//) {
			$s += tz_to_s_offset($tz);
		} elsif ($tz =~ s/^\-//) {
			$s -= tz_to_s_offset($tz);
		}
		eval { command_close_pipe($fh) };
		return $s;
	}
	die "Can't get commit time for commit: $cmt\n";
}

sub tz_to_s_offset {
	my ($tz) = @_;
	$tz =~ s/(\d\d)$//;
	return ($1 * 60) + ($tz * 3600);
}

# adapted from pager.c
sub config_pager {
	$_pager ||= $ENV{GIT_PAGER} || $ENV{PAGER};
	if (!defined $_pager) {
		$_pager = 'less';
	} elsif (length $_pager == 0 || $_pager eq 'cat') {
		$_pager = undef;
	}
}

sub run_pager {
	return unless -t *STDOUT;
	pipe my $rfd, my $wfd or return;
	defined(my $pid = fork) or croak $!;
	if (!$pid) {
		open STDOUT, '>&', $wfd or croak $!;
		return;
	}
	open STDIN, '<&', $rfd or croak $!;
	$ENV{LESS} ||= 'FRSX';
	exec $_pager or croak "Can't run pager: $! ($_pager)\n";
}

sub get_author_info {
	my ($dest, $author, $t, $tz) = @_;
	$author =~ s/(?:^\s*|\s*$)//g;
	$dest->{a_raw} = $author;
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

sub show_commit_changed_paths {
	my ($c) = @_;
	return unless $c->{changed};
	print "Changed paths:\n", @{$c->{changed}};
}

sub show_commit_normal {
	my ($c) = @_;
	print '-' x72, "\nr$c->{r} | ";
	print "$c->{c} | " if $_show_commit;
	print "$c->{a} | ", strftime("%Y-%m-%d %H:%M:%S %z (%a, %d %b %Y)",
				 localtime($c->{t_utc})), ' | ';
	my $nr_line = 0;

	if (my $l = $c->{l}) {
		while ($l->[$#$l] eq "\n" && $#$l > 0
		                          && $l->[($#$l - 1)] eq "\n") {
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
			print $nr_line, "\n";
			show_commit_changed_paths($c);
			print "\n";
			print $_ foreach @$l;
		}
	} else {
		print "1 line\n";
		show_commit_changed_paths($c);
		print "\n";

	}
	foreach my $x (qw/raw diff/) {
		if ($c->{$x}) {
			print "\n";
			print $_ foreach @{$c->{$x}}
		}
	}
}

sub _simple_prompt {
	my ($cred, $realm, $default_username, $may_save, $pool) = @_;
	$may_save = undef if $_no_auth_cache;
	$default_username = $_username if defined $_username;
	if (defined $default_username && length $default_username) {
		if (defined $realm && length $realm) {
			print STDERR "Authentication realm: $realm\n";
			STDERR->flush;
		}
		$cred->username($default_username);
	} else {
		_username_prompt($cred, $realm, $may_save, $pool);
	}
	$cred->password(_read_password("Password for '" .
	                               $cred->username . "': ", $realm));
	$cred->may_save($may_save);
	$SVN::_Core::SVN_NO_ERROR;
}

sub _ssl_server_trust_prompt {
	my ($cred, $realm, $failures, $cert_info, $may_save, $pool) = @_;
	$may_save = undef if $_no_auth_cache;
	print STDERR "Error validating server certificate for '$realm':\n";
	if ($failures & $SVN::Auth::SSL::UNKNOWNCA) {
		print STDERR " - The certificate is not issued by a trusted ",
		      "authority. Use the\n",
	              "   fingerprint to validate the certificate manually!\n";
	}
	if ($failures & $SVN::Auth::SSL::CNMISMATCH) {
		print STDERR " - The certificate hostname does not match.\n";
	}
	if ($failures & $SVN::Auth::SSL::NOTYETVALID) {
		print STDERR " - The certificate is not yet valid.\n";
	}
	if ($failures & $SVN::Auth::SSL::EXPIRED) {
		print STDERR " - The certificate has expired.\n";
	}
	if ($failures & $SVN::Auth::SSL::OTHER) {
		print STDERR " - The certificate has an unknown error.\n";
	}
	printf STDERR
	        "Certificate information:\n".
	        " - Hostname: %s\n".
	        " - Valid: from %s until %s\n".
	        " - Issuer: %s\n".
	        " - Fingerprint: %s\n",
	        map $cert_info->$_, qw(hostname valid_from valid_until
	                               issuer_dname fingerprint);
	my $choice;
prompt:
	print STDERR $may_save ?
	      "(R)eject, accept (t)emporarily or accept (p)ermanently? " :
	      "(R)eject or accept (t)emporarily? ";
	STDERR->flush;
	$choice = lc(substr(<STDIN> || 'R', 0, 1));
	if ($choice =~ /^t$/i) {
		$cred->may_save(undef);
	} elsif ($choice =~ /^r$/i) {
		return -1;
	} elsif ($may_save && $choice =~ /^p$/i) {
		$cred->may_save($may_save);
	} else {
		goto prompt;
	}
	$cred->accepted_failures($failures);
	$SVN::_Core::SVN_NO_ERROR;
}

sub _ssl_client_cert_prompt {
	my ($cred, $realm, $may_save, $pool) = @_;
	$may_save = undef if $_no_auth_cache;
	print STDERR "Client certificate filename: ";
	STDERR->flush;
	chomp(my $filename = <STDIN>);
	$cred->cert_file($filename);
	$cred->may_save($may_save);
	$SVN::_Core::SVN_NO_ERROR;
}

sub _ssl_client_cert_pw_prompt {
	my ($cred, $realm, $may_save, $pool) = @_;
	$may_save = undef if $_no_auth_cache;
	$cred->password(_read_password("Password: ", $realm));
	$cred->may_save($may_save);
	$SVN::_Core::SVN_NO_ERROR;
}

sub _username_prompt {
	my ($cred, $realm, $may_save, $pool) = @_;
	$may_save = undef if $_no_auth_cache;
	if (defined $realm && length $realm) {
		print STDERR "Authentication realm: $realm\n";
	}
	my $username;
	if (defined $_username) {
		$username = $_username;
	} else {
		print STDERR "Username: ";
		STDERR->flush;
		chomp($username = <STDIN>);
	}
	$cred->username($username);
	$cred->may_save($may_save);
	$SVN::_Core::SVN_NO_ERROR;
}

sub _read_password {
	my ($prompt, $realm) = @_;
	print STDERR $prompt;
	STDERR->flush;
	require Term::ReadKey;
	Term::ReadKey::ReadMode('noecho');
	my $password = '';
	while (defined(my $key = Term::ReadKey::ReadKey(0))) {
		last if $key =~ /[\012\015]/; # \n\r
		$password .= $key;
	}
	Term::ReadKey::ReadMode('restore');
	print STDERR "\n";
	STDERR->flush;
	$password;
}

sub libsvn_connect {
	my ($url) = @_;
	SVN::_Core::svn_config_ensure($_config_dir, undef);
	my ($baton, $callbacks) = SVN::Core::auth_open_helper([
	    SVN::Client::get_simple_provider(),
	    SVN::Client::get_ssl_server_trust_file_provider(),
	    SVN::Client::get_simple_prompt_provider(
	      \&_simple_prompt, 2),
	    SVN::Client::get_ssl_client_cert_prompt_provider(
	      \&_ssl_client_cert_prompt, 2),
	    SVN::Client::get_ssl_client_cert_pw_prompt_provider(
	      \&_ssl_client_cert_pw_prompt, 2),
	    SVN::Client::get_username_provider(),
	    SVN::Client::get_ssl_server_trust_prompt_provider(
	      \&_ssl_server_trust_prompt),
	    SVN::Client::get_username_prompt_provider(
	      \&_username_prompt, 2),
	  ]);
	my $config = SVN::Core::config_get_config($_config_dir);
	my $ra = SVN::Ra->new(url => $url, auth => $baton,
	                      config => $config,
	                      pool => SVN::Pool->new,
	                      auth_provider_callbacks => $callbacks);
	$ra->{svn_path} = $url;
	$ra->{repos_root} = $ra->get_repos_root;
	$ra->{svn_path} =~ s#^\Q$ra->{repos_root}\E/*##;
	push @repo_path_split_cache, qr/^(\Q$ra->{repos_root}\E)/;
	return $ra;
}

sub libsvn_can_do_switch {
	unless (defined $_svn_can_do_switch) {
		my $pool = SVN::Pool->new;
		my $rep = eval {
			$SVN->do_switch(1, '', 0, $SVN->{url},
			                SVN::Delta::Editor->new, $pool);
		};
		if ($@) {
			$_svn_can_do_switch = 0;
		} else {
			$rep->abort_report($pool);
			$_svn_can_do_switch = 1;
		}
		$pool->clear;
	}
	$_svn_can_do_switch;
}

sub libsvn_dup_ra {
	my ($ra) = @_;
	SVN::Ra->new(map { $_ => $ra->{$_} } qw/config url
	             auth auth_provider_callbacks repos_root svn_path/);
}

sub uri_encode {
	my ($f) = @_;
	$f =~ s#([^a-zA-Z0-9\*!\:_\./\-])#uc sprintf("%%%02x",ord($1))#eg;
	$f
}

sub uri_decode {
	my ($f) = @_;
	$f =~ tr/+/ /;
	$f =~ s/%([A-F0-9]{2})/chr hex($1)/ge;
	$f
}

sub libsvn_log_entry {
	my ($rev, $author, $date, $msg, $parents, $untracked) = @_;
	my ($Y,$m,$d,$H,$M,$S) = ($date =~ /^(\d{4})\-(\d\d)\-(\d\d)T
					 (\d\d)\:(\d\d)\:(\d\d).\d+Z$/x)
				or die "Unable to parse date: $date\n";
	if (defined $author && length $author > 0 &&
	    defined $_authors && ! defined $users{$author}) {
		die "Author: $author not defined in $_authors file\n";
	}
	$msg = '' if ($rev == 0 && !defined $msg);

	open my $un, '>>', "$GIT_SVN_DIR/unhandled.log" or croak $!;
	my $h;
	print $un "r$rev\n" or croak $!;
	$h = $untracked->{empty};
	foreach (sort keys %$h) {
		my $act = $h->{$_} ? '+empty_dir' : '-empty_dir';
		print $un "  $act: ", uri_encode($_), "\n" or croak $!;
		warn "W: $act: $_\n";
	}
	foreach my $t (qw/dir_prop file_prop/) {
		$h = $untracked->{$t} or next;
		foreach my $path (sort keys %$h) {
			my $ppath = $path eq '' ? '.' : $path;
			foreach my $prop (sort keys %{$h->{$path}}) {
				next if $SKIP{$prop};
				my $v = $h->{$path}->{$prop};
				if (defined $v) {
					print $un "  +$t: ",
						  uri_encode($ppath), ' ',
						  uri_encode($prop), ' ',
						  uri_encode($v), "\n"
						  or croak $!;
				} else {
					print $un "  -$t: ",
						  uri_encode($ppath), ' ',
						  uri_encode($prop), "\n"
						  or croak $!;
				}
			}
		}
	}
	foreach my $t (qw/absent_file absent_directory/) {
		$h = $untracked->{$t} or next;
		foreach my $parent (sort keys %$h) {
			foreach my $path (sort @{$h->{$parent}}) {
				print $un "  $t: ",
				      uri_encode("$parent/$path"), "\n"
				      or croak $!;
				warn "W: $t: $parent/$path ",
				     "Insufficient permissions?\n";
			}
		}
	}

	# revprops (make this optional? it's an extra network trip...)
	my $pool = SVN::Pool->new;
	my $rp = $SVN->rev_proplist($rev, $pool);
	foreach (sort keys %$rp) {
		next if /^svn:(?:author|date|log)$/;
		print $un "  rev_prop: ", uri_encode($_), ' ',
		          uri_encode($rp->{$_}), "\n";
	}
	$pool->clear;
	close $un or croak $!;

	{ revision => $rev, date => "+0000 $Y-$m-$d $H:$M:$S",
	  author => $author, msg => $msg."\n", parents => $parents || [],
	  revprops => $rp }
}

sub process_rm {
	my ($gui, $last_commit, $f, $q) = @_;
	# remove entire directories.
	if (command('ls-tree',$last_commit,'--',$f) =~ /^040000 tree/) {
		my ($ls, $ctx) = command_output_pipe(qw/ls-tree
		                                     -r --name-only -z/,
				                     $last_commit,'--',$f);
		local $/ = "\0";
		while (<$ls>) {
			print $gui '0 ',0 x 40,"\t",$_ or croak $!;
			print "\tD\t$_\n" unless $q;
		}
		print "\tD\t$f/\n" unless $q;
		command_close_pipe($ls, $ctx);
		return $SVN::Node::dir;
	} else {
		print $gui '0 ',0 x 40,"\t",$f,"\0" or croak $!;
		print "\tD\t$f\n" unless $q;
		return $SVN::Node::file;
	}
}

sub libsvn_fetch {
	my ($last_commit, $paths, $rev, $author, $date, $msg) = @_;
	my $pool = SVN::Pool->new;
	my $ed = SVN::Git::Fetcher->new({ c => $last_commit, q => $_q });
	my $reporter = $SVN->do_update($rev, '', 1, $ed, $pool);
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef) : ();
	my (undef, $last_rev, undef) = cmt_metadata($last_commit);
	$reporter->set_path('', $last_rev, 0, @lock, $pool);
	$reporter->finish_report($pool);
	$pool->clear;
	unless ($ed->{git_commit_ok}) {
		die "SVN connection failed somewhere...\n";
	}
	libsvn_log_entry($rev, $author, $date, $msg, [$last_commit], $ed);
}

sub svn_grab_base_rev {
	my $c = eval { command_oneline([qw/rev-parse --verify/,
	                                "refs/remotes/$GIT_SVN^0"],
				        { STDERR => 0 }) };
	if (defined $c && length $c) {
		my ($url, $rev, $uuid) = cmt_metadata($c);
		return ($rev, $c) if defined $rev;
	}
	if ($_no_metadata) {
		my $offset = -41; # from tail
		my $rl;
		open my $fh, '<', $REVDB or
			die "--no-metadata specified and $REVDB not readable\n";
		seek $fh, $offset, 2;
		$rl = readline $fh;
		defined $rl or return (undef, undef);
		chomp $rl;
		while ($c ne $rl && tell $fh != 0) {
			$offset -= 41;
			seek $fh, $offset, 2;
			$rl = readline $fh;
			defined $rl or return (undef, undef);
			chomp $rl;
		}
		my $rev = tell $fh;
		croak $! if ($rev < -1);
		$rev =  ($rev - 41) / 41;
		close $fh or croak $!;
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

sub libsvn_traverse_ignore {
	my ($fh, $path, $r) = @_;
	$path =~ s#^/+##g;
	my $pool = SVN::Pool->new;
	my ($dirent, undef, $props) = $SVN->get_dir($path, $r, $pool);
	my $p = $path;
	$p =~ s#^\Q$SVN->{svn_path}\E/##;
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
	# should be OK to use Pool here (r1 - r0) should be small
	my $pool = SVN::Pool->new;
	libsvn_get_log($SVN, [$path], $r0, $r1,
			0, 0, 1, sub {$nr++}, $pool);
	$pool->clear;
	return 0 if ($nr > 1);
	return 1;
}

sub libsvn_find_parent_branch {
	my ($paths, $rev, $author, $date, $msg) = @_;
	my $svn_path = '/'.$SVN->{svn_path};

	# look for a parent from another branch:
	my $i = $paths->{$svn_path} or return;
	my $branch_from = $i->copyfrom_path or return;
	my $r = $i->copyfrom_rev;
	print STDERR  "Found possible branch point: ",
				"$branch_from => $svn_path, $r\n";
	$branch_from =~ s#^/##;
	my $l_map = {};
	read_url_paths_all($l_map, '', "$GIT_DIR/svn");
	my $url = $SVN->{repos_root};
	defined $l_map->{$url} or return;
	my $id = $l_map->{$url}->{$branch_from};
	if (!defined $id && $_follow_parent) {
		print STDERR "Following parent: $branch_from\@$r\n";
		# auto create a new branch and follow it
		$id = basename($branch_from);
		$id .= '@'.$r if -r "$GIT_DIR/svn/$id";
		while (-r "$GIT_DIR/svn/$id") {
			# just grow a tail if we're not unique enough :x
			$id .= '-';
		}
	}
	return unless defined $id;

	my ($r0, $parent) = find_rev_before($r,$id,1);
	if ($_follow_parent && (!defined $r0 || !defined $parent)) {
		defined(my $pid = fork) or croak $!;
		if (!$pid) {
			$GIT_SVN = $ENV{GIT_SVN_ID} = $id;
			init_vars();
			$SVN_URL = "$url/$branch_from";
			$SVN = undef;
			setup_git_svn();
			# we can't assume SVN_URL exists at r+1:
			$_revision = "0:$r";
			fetch_lib();
			exit 0;
		}
		waitpid $pid, 0;
		croak $? if $?;
		($r0, $parent) = find_rev_before($r,$id,1);
	}
	return unless (defined $r0 && defined $parent);
	if (revisions_eq($branch_from, $r0, $r)) {
		unlink $GIT_SVN_INDEX;
		print STDERR "Found branch parent: ($GIT_SVN) $parent\n";
		command_noisy('read-tree', $parent);
		unless (libsvn_can_do_switch()) {
			return _libsvn_new_tree($paths, $rev, $author, $date,
			                        $msg, [$parent]);
		}
		# do_switch works with svn/trunk >= r22312, but that is not
		# included with SVN 1.4.2 (the latest version at the moment),
		# so we can't rely on it.
		my $ra = libsvn_connect("$url/$branch_from");
		my $ed = SVN::Git::Fetcher->new({c => $parent, q => $_q });
		my $pool = SVN::Pool->new;
		my $reporter = $ra->do_switch($rev, '', 1, $SVN->{url},
		                              $ed, $pool);
		my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef) : ();
		$reporter->set_path('', $r0, 0, @lock, $pool);
		$reporter->finish_report($pool);
		$pool->clear;
		unless ($ed->{git_commit_ok}) {
			die "SVN connection failed somewhere...\n";
		}
		return libsvn_log_entry($rev, $author, $date, $msg, [$parent]);
	}
	print STDERR "Nope, branch point not imported or unknown\n";
	return undef;
}

sub libsvn_get_log {
	my ($ra, @args) = @_;
	$args[4]-- if $args[4] && ! $_follow_parent;
	if ($SVN::Core::VERSION le '1.2.0') {
		splice(@args, 3, 1);
	}
	$ra->get_log(@args);
}

sub libsvn_new_tree {
	if (my $log_entry = libsvn_find_parent_branch(@_)) {
		return $log_entry;
	}
	my ($paths, $rev, $author, $date, $msg) = @_; # $pool is last
	_libsvn_new_tree($paths, $rev, $author, $date, $msg, []);
}

sub _libsvn_new_tree {
	my ($paths, $rev, $author, $date, $msg, $parents) = @_;
	my $pool = SVN::Pool->new;
	my $ed = SVN::Git::Fetcher->new({q => $_q});
	my $reporter = $SVN->do_update($rev, '', 1, $ed, $pool);
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef) : ();
	$reporter->set_path('', $rev, 1, @lock, $pool);
	$reporter->finish_report($pool);
	$pool->clear;
	unless ($ed->{git_commit_ok}) {
		die "SVN connection failed somewhere...\n";
	}
	libsvn_log_entry($rev, $author, $date, $msg, $parents, $ed);
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
			my ($url_b, undef, $uuid_b) = cmt_metadata($c);
			my ($url_a, undef, $uuid_a) = cmt_metadata($parent);
			next if ($url_a && $url_b && $url_a eq $url_b &&
							$uuid_b eq $uuid_a);
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
		my @diff = command('diff-tree', $cmt, $c);
		if (@diff) {
			print STDERR "Trees differ: $cmt $c\n",
					join('',@diff),"\n";
			exit 1;
		}
	} else {
		fetch("$rev=$c");
	}
}

sub libsvn_ls_fullurl {
	my $fullurl = shift;
	my $ra = libsvn_connect($fullurl);
	my @ret;
	my $pool = SVN::Pool->new;
	my $r = defined $_revision ? $_revision : $ra->get_latest_revnum;
	my ($dirent, undef, undef) = $ra->get_dir('', $r, $pool);
	foreach my $d (sort keys %$dirent) {
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
	# 175007 - http(s):// (this repo required authorization, too...)
	#   More codes may be discovered later...
	if ($errno == 175007 || $errno == 175002 || $errno == 160013) {
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

sub copy_remote_ref {
	my $origin = $_cp_remote ? $_cp_remote : 'origin';
	my $ref = "refs/remotes/$GIT_SVN";
	if (command('ls-remote', $origin, $ref)) {
		command_noisy('fetch', $origin, "$ref:$ref");
	} elsif ($_cp_remote && !$_upgrade) {
		die "Unable to find remote reference: ",
				"refs/remotes/$GIT_SVN on $origin\n";
	}
}

{
	my $kill_stupid_warnings = $SVN::Node::none.$SVN::Node::file.
				$SVN::Node::dir.$SVN::Node::unknown.
				$SVN::Node::none.$SVN::Node::file.
				$SVN::Node::dir.$SVN::Node::unknown.
				$SVN::Auth::SSL::CNMISMATCH.
				$SVN::Auth::SSL::NOTYETVALID.
				$SVN::Auth::SSL::EXPIRED.
				$SVN::Auth::SSL::UNKNOWNCA.
				$SVN::Auth::SSL::OTHER;
}

package SVN::Git::Fetcher;
use vars qw/@ISA/;
use strict;
use warnings;
use Carp qw/croak/;
use IO::File qw//;
use Git qw/command command_oneline command_noisy
           command_output_pipe command_input_pipe command_close_pipe/;

# file baton members: path, mode_a, mode_b, pool, fh, blob, base
sub new {
	my ($class, $git_svn) = @_;
	my $self = SVN::Delta::Editor->new;
	bless $self, $class;
	$self->{c} = $git_svn->{c} if exists $git_svn->{c};
	$self->{q} = $git_svn->{q};
	$self->{empty} = {};
	$self->{dir_prop} = {};
	$self->{file_prop} = {};
	$self->{absent_dir} = {};
	$self->{absent_file} = {};
	($self->{gui}, $self->{ctx}) = command_input_pipe(
	                                     qw/update-index -z --index-info/);
	require Digest::MD5;
	$self;
}

sub open_root {
	{ path => '' };
}

sub open_directory {
	my ($self, $path, $pb, $rev) = @_;
	{ path => $path };
}

sub delete_entry {
	my ($self, $path, $rev, $pb) = @_;
	my $t = process_rm($self->{gui}, $self->{c}, $path, $self->{q});
	$self->{empty}->{$path} = 0 if $t == $SVN::Node::dir;
	undef;
}

sub open_file {
	my ($self, $path, $pb, $rev) = @_;
	my ($mode, $blob) = (command('ls-tree', $self->{c}, '--',$path)
	                     =~ /^(\d{6}) blob ([a-f\d]{40})\t/);
	unless (defined $mode && defined $blob) {
		die "$path was not found in commit $self->{c} (r$rev)\n";
	}
	{ path => $path, mode_a => $mode, mode_b => $mode, blob => $blob,
	  pool => SVN::Pool->new, action => 'M' };
}

sub add_file {
	my ($self, $path, $pb, $cp_path, $cp_rev) = @_;
	my ($dir, $file) = ($path =~ m#^(.*?)/?([^/]+)$#);
	delete $self->{empty}->{$dir};
	{ path => $path, mode_a => 100644, mode_b => 100644,
	  pool => SVN::Pool->new, action => 'A' };
}

sub add_directory {
	my ($self, $path, $cp_path, $cp_rev) = @_;
	my ($dir, $file) = ($path =~ m#^(.*?)/?([^/]+)$#);
	delete $self->{empty}->{$dir};
	$self->{empty}->{$path} = 1;
	{ path => $path };
}

sub change_dir_prop {
	my ($self, $db, $prop, $value) = @_;
	$self->{dir_prop}->{$db->{path}} ||= {};
	$self->{dir_prop}->{$db->{path}}->{$prop} = $value;
	undef;
}

sub absent_directory {
	my ($self, $path, $pb) = @_;
	$self->{absent_dir}->{$pb->{path}} ||= [];
	push @{$self->{absent_dir}->{$pb->{path}}}, $path;
	undef;
}

sub absent_file {
	my ($self, $path, $pb) = @_;
	$self->{absent_file}->{$pb->{path}} ||= [];
	push @{$self->{absent_file}->{$pb->{path}}}, $path;
	undef;
}

sub change_file_prop {
	my ($self, $fb, $prop, $value) = @_;
	if ($prop eq 'svn:executable') {
		if ($fb->{mode_b} != 120000) {
			$fb->{mode_b} = defined $value ? 100755 : 100644;
		}
	} elsif ($prop eq 'svn:special') {
		$fb->{mode_b} = defined $value ? 120000 : 100644;
	} else {
		$self->{file_prop}->{$fb->{path}} ||= {};
		$self->{file_prop}->{$fb->{path}}->{$prop} = $value;
	}
	undef;
}

sub apply_textdelta {
	my ($self, $fb, $exp) = @_;
	my $fh = IO::File->new_tmpfile;
	$fh->autoflush(1);
	# $fh gets auto-closed() by SVN::TxDelta::apply(),
	# (but $base does not,) so dup() it for reading in close_file
	open my $dup, '<&', $fh or croak $!;
	my $base = IO::File->new_tmpfile;
	$base->autoflush(1);
	if ($fb->{blob}) {
		defined (my $pid = fork) or croak $!;
		if (!$pid) {
			open STDOUT, '>&', $base or croak $!;
			print STDOUT 'link ' if ($fb->{mode_a} == 120000);
			exec qw/git-cat-file blob/, $fb->{blob} or croak $!;
		}
		waitpid $pid, 0;
		croak $? if $?;

		if (defined $exp) {
			seek $base, 0, 0 or croak $!;
			my $md5 = Digest::MD5->new;
			$md5->addfile($base);
			my $got = $md5->hexdigest;
			die "Checksum mismatch: $fb->{path} $fb->{blob}\n",
			    "expected: $exp\n",
			    "     got: $got\n" if ($got ne $exp);
		}
	}
	seek $base, 0, 0 or croak $!;
	$fb->{fh} = $dup;
	$fb->{base} = $base;
	[ SVN::TxDelta::apply($base, $fh, undef, $fb->{path}, $fb->{pool}) ];
}

sub close_file {
	my ($self, $fb, $exp) = @_;
	my $hash;
	my $path = $fb->{path};
	if (my $fh = $fb->{fh}) {
		seek($fh, 0, 0) or croak $!;
		my $md5 = Digest::MD5->new;
		$md5->addfile($fh);
		my $got = $md5->hexdigest;
		die "Checksum mismatch: $path\n",
		    "expected: $exp\n    got: $got\n" if ($got ne $exp);
		seek($fh, 0, 0) or croak $!;
		if ($fb->{mode_b} == 120000) {
			read($fh, my $buf, 5) == 5 or croak $!;
			$buf eq 'link ' or die "$path has mode 120000",
			                       "but is not a link\n";
		}
		defined(my $pid = open my $out,'-|') or die "Can't fork: $!\n";
		if (!$pid) {
			open STDIN, '<&', $fh or croak $!;
			exec qw/git-hash-object -w --stdin/ or croak $!;
		}
		chomp($hash = do { local $/; <$out> });
		close $out or croak $!;
		close $fh or croak $!;
		$hash =~ /^[a-f\d]{40}$/ or die "not a sha1: $hash\n";
		close $fb->{base} or croak $!;
	} else {
		$hash = $fb->{blob} or die "no blob information\n";
	}
	$fb->{pool}->clear;
	my $gui = $self->{gui};
	print $gui "$fb->{mode_b} $hash\t$path\0" or croak $!;
	print "\t$fb->{action}\t$path\n" if $fb->{action} && ! $self->{q};
	undef;
}

sub abort_edit {
	my $self = shift;
	eval { command_close_pipe($self->{gui}, $self->{ctx}) };
	$self->SUPER::abort_edit(@_);
}

sub close_edit {
	my $self = shift;
	command_close_pipe($self->{gui}, $self->{ctx});
	$self->{git_commit_ok} = 1;
	$self->SUPER::close_edit(@_);
}

package SVN::Git::Editor;
use vars qw/@ISA/;
use strict;
use warnings;
use Carp qw/croak/;
use IO::File;
use Git qw/command command_oneline command_noisy
           command_output_pipe command_input_pipe command_close_pipe/;

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
	(defined $_[1] && length $_[1]) ? $_[1] : ''
}

sub url_path {
	my ($self, $path) = @_;
	$self->{ra}->{url} . '/' . $self->repo_path($path);
}

sub rmdirs {
	my ($self, $q) = @_;
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

	my ($fh, $ctx) = command_output_pipe(
	                           qw/ls-tree --name-only -r -z/, $self->{c});
	local $/ = "\0";
	while (<$fh>) {
		chomp;
		my @dn = split m#/#, $_;
		while (pop @dn) {
			delete $rm->{join '/', @dn};
		}
		unless (%$rm) {
			eval { command_close_pipe($fh) };
			return;
		}
	}
	command_close_pipe($fh, $ctx);

	my ($r, $p, $bat) = ($self->{r}, $self->{pool}, $self->{bat});
	foreach my $d (sort { $b =~ tr#/#/# <=> $a =~ tr#/#/# } keys %$rm) {
		$self->close_directory($bat->{$d}, $p);
		my ($dn) = ($d =~ m#^(.*?)/?(?:[^/]+)$#);
		print "\tD+\t$d/\n" unless $q;
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
	my ($self, $m, $q) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
					undef, -1);
	print "\tA\t$m->{file_b}\n" unless $q;
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});
}

sub C {
	my ($self, $m, $q) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
				$self->url_path($m->{file_a}), $self->{r});
	print "\tC\t$m->{file_a} => $m->{file_b}\n" unless $q;
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
	my ($self, $m, $q) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
				$self->url_path($m->{file_a}), $self->{r});
	print "\tR\t$m->{file_a} => $m->{file_b}\n" unless $q;
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});

	($dir, $file) = split_path($m->{file_a});
	$pbat = $self->ensure_path($dir);
	$self->delete_entry($m->{file_a}, $pbat);
}

sub M {
	my ($self, $m, $q) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->open_file($self->repo_path($m->{file_b}),
				$pbat,$self->{r},$self->{pool});
	print "\t$m->{chg}\t$m->{file_b}\n" unless $q;
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
	my $pool = SVN::Pool->new;
	my $atd = $self->apply_textdelta($fbat, undef, $pool);
	my $got = SVN::TxDelta::send_stream($fh, @$atd, $pool);
	die "Checksum mismatch\nexpected: $exp\ngot: $got\n" if ($got ne $exp);
	$pool->clear;

	close $fh or croak $!;
}

sub D {
	my ($self, $m, $q) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	print "\tD\t$m->{file_b}\n" unless $q;
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

$log_msg hashref as returned by libsvn_log_entry()
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

# retval of read_url_paths{,_all}();
$l_map = {
	# repository root url
	'https://svn.musicpd.org' => {
		# repository path 		# GIT_SVN_ID
		'mpd/trunk'		=>	'trunk',
		'mpd/tags/0.11.5'	=>	'tags/0.11.5',
	},
}

Notes:
	I don't trust the each() function on unless I created %hash myself
	because the internal iterator may not have started at base.
