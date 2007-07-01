#!/usr/bin/env perl
# Copyright (C) 2006, Eric Wong <normalperson@yhbt.net>
# License: GPL v2 or later
use warnings;
use strict;
use vars qw/	$AUTHOR $VERSION
		$sha1 $sha1_short $_revision
		$_q $_authors %users/;
$AUTHOR = 'Eric Wong <normalperson@yhbt.net>';
$VERSION = '@@GIT_VERSION@@';

my $git_dir_user_set = 1 if defined $ENV{GIT_DIR};
$ENV{GIT_DIR} ||= '.git';
$Git::SVN::default_repo_id = 'svn';
$Git::SVN::default_ref_id = $ENV{GIT_SVN_ID} || 'git-svn';
$Git::SVN::Ra::_log_window_size = 100;

$Git::SVN::Log::TZ = $ENV{TZ};
$ENV{TZ} = 'UTC';
$| = 1; # unbuffer STDOUT

sub fatal (@) { print STDERR @_; exit 1 }
require SVN::Core; # use()-ing this causes segfaults for me... *shrug*
require SVN::Ra;
require SVN::Delta;
if ($SVN::Core::VERSION lt '1.1.0') {
	fatal "Need SVN::Core 1.1.0 or better (got $SVN::Core::VERSION)\n";
}
push @Git::SVN::Ra::ISA, 'SVN::Ra';
push @SVN::Git::Editor::ISA, 'SVN::Delta::Editor';
push @SVN::Git::Fetcher::ISA, 'SVN::Delta::Editor';
use Carp qw/croak/;
use IO::File qw//;
use File::Basename qw/dirname basename/;
use File::Path qw/mkpath/;
use Getopt::Long qw/:config gnu_getopt no_ignore_case auto_abbrev/;
use IPC::Open3;
use Git;

BEGIN {
	# import functions from Git into our packages, en masse
	no strict 'refs';
	foreach (qw/command command_oneline command_noisy command_output_pipe
	            command_input_pipe command_close_pipe/) {
		for my $package ( qw(SVN::Git::Editor SVN::Git::Fetcher
			Git::SVN::Migration Git::SVN::Log Git::SVN),
			__PACKAGE__) {
			*{"${package}::$_"} = \&{"Git::$_"};
		}
	}
}

my ($SVN);

$sha1 = qr/[a-f\d]{40}/;
$sha1_short = qr/[a-f\d]{4,40}/;
my ($_stdin, $_help, $_edit,
	$_message, $_file,
	$_template, $_shared,
	$_version, $_fetch_all, $_no_rebase,
	$_merge, $_strategy, $_dry_run, $_local,
	$_prefix, $_no_checkout, $_verbose);
$Git::SVN::_follow_parent = 1;
my %remote_opts = ( 'username=s' => \$Git::SVN::Prompt::_username,
                    'config-dir=s' => \$Git::SVN::Ra::config_dir,
                    'no-auth-cache' => \$Git::SVN::Prompt::_no_auth_cache );
my %fc_opts = ( 'follow-parent|follow!' => \$Git::SVN::_follow_parent,
		'authors-file|A=s' => \$_authors,
		'repack:i' => \$Git::SVN::_repack,
		'noMetadata' => \$Git::SVN::_no_metadata,
		'useSvmProps' => \$Git::SVN::_use_svm_props,
		'useSvnsyncProps' => \$Git::SVN::_use_svnsync_props,
		'log-window-size=i' => \$Git::SVN::Ra::_log_window_size,
		'no-checkout' => \$_no_checkout,
		'quiet|q' => \$_q,
		'repack-flags|repack-args|repack-opts=s' =>
		   \$Git::SVN::_repack_flags,
		%remote_opts );

my ($_trunk, $_tags, $_branches);
my %icv;
my %init_opts = ( 'template=s' => \$_template, 'shared:s' => \$_shared,
                  'trunk|T=s' => \$_trunk, 'tags|t=s' => \$_tags,
                  'branches|b=s' => \$_branches, 'prefix=s' => \$_prefix,
                  'minimize-url|m' => \$Git::SVN::_minimize_url,
		  'no-metadata' => sub { $icv{noMetadata} = 1 },
		  'use-svm-props' => sub { $icv{useSvmProps} = 1 },
		  'use-svnsync-props' => sub { $icv{useSvnsyncProps} = 1 },
		  'rewrite-root=s' => sub { $icv{rewriteRoot} = $_[1] },
                  %remote_opts );
my %cmt_opts = ( 'edit|e' => \$_edit,
		'rmdir' => \$SVN::Git::Editor::_rmdir,
		'find-copies-harder' => \$SVN::Git::Editor::_find_copies_harder,
		'l=i' => \$SVN::Git::Editor::_rename_limit,
		'copy-similarity|C=i'=> \$SVN::Git::Editor::_cp_similarity
);

my %cmd = (
	fetch => [ \&cmd_fetch, "Download new revisions from SVN",
			{ 'revision|r=s' => \$_revision,
			  'fetch-all|all' => \$_fetch_all,
			   %fc_opts } ],
	clone => [ \&cmd_clone, "Initialize and fetch revisions",
			{ 'revision|r=s' => \$_revision,
			   %fc_opts, %init_opts } ],
	init => [ \&cmd_init, "Initialize a repo for tracking" .
			  " (requires URL argument)",
			  \%init_opts ],
	'multi-init' => [ \&cmd_multi_init,
	                  "Deprecated alias for ".
			  "'$0 init -T<trunk> -b<branches> -t<tags>'",
			  \%init_opts ],
	dcommit => [ \&cmd_dcommit,
	             'Commit several diffs to merge with upstream',
			{ 'merge|m|M' => \$_merge,
			  'strategy|s=s' => \$_strategy,
			  'verbose|v' => \$_verbose,
			  'dry-run|n' => \$_dry_run,
			  'fetch-all|all' => \$_fetch_all,
			  'no-rebase' => \$_no_rebase,
			%cmt_opts, %fc_opts } ],
	'set-tree' => [ \&cmd_set_tree,
	                "Set an SVN repository to a git tree-ish",
			{ 'stdin|' => \$_stdin, %cmt_opts, %fc_opts, } ],
	'show-ignore' => [ \&cmd_show_ignore, "Show svn:ignore listings",
			{ 'revision|r=i' => \$_revision } ],
	'multi-fetch' => [ \&cmd_multi_fetch,
	                   "Deprecated alias for $0 fetch --all",
			   { 'revision|r=s' => \$_revision, %fc_opts } ],
	'migrate' => [ sub { },
	               # no-op, we automatically run this anyways,
	               'Migrate configuration/metadata/layout from
		        previous versions of git-svn',
                       { 'minimize' => \$Git::SVN::Migration::_minimize,
			 %remote_opts } ],
	'log' => [ \&Git::SVN::Log::cmd_show_log, 'Show commit logs',
			{ 'limit=i' => \$Git::SVN::Log::limit,
			  'revision|r=s' => \$_revision,
			  'verbose|v' => \$Git::SVN::Log::verbose,
			  'incremental' => \$Git::SVN::Log::incremental,
			  'oneline' => \$Git::SVN::Log::oneline,
			  'show-commit' => \$Git::SVN::Log::show_commit,
			  'non-recursive' => \$Git::SVN::Log::non_recursive,
			  'authors-file|A=s' => \$_authors,
			  'color' => \$Git::SVN::Log::color,
			  'pager=s' => \$Git::SVN::Log::pager,
			} ],
	'find-rev' => [ \&cmd_find_rev, "Translate between SVN revision numbers and tree-ish",
			{ } ],
	'rebase' => [ \&cmd_rebase, "Fetch and rebase your working directory",
			{ 'merge|m|M' => \$_merge,
			  'verbose|v' => \$_verbose,
			  'strategy|s=s' => \$_strategy,
			  'local|l' => \$_local,
			  'fetch-all|all' => \$_fetch_all,
			  %fc_opts } ],
	'commit-diff' => [ \&cmd_commit_diff,
	                   'Commit a diff between two trees',
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
Getopt::Long::Configure('pass_through') if ($cmd && $cmd eq 'log');
my $rv = GetOptions(%opts, 'help|H|h' => \$_help, 'version|V' => \$_version,
                    'minimize-connections' => \$Git::SVN::Migration::_minimize,
                    'id|i=s' => \$Git::SVN::default_ref_id,
                    'svn-remote|remote|R=s' => sub {
                       $Git::SVN::no_reuse_existing = 1;
                       $Git::SVN::default_repo_id = $_[1] });
exit 1 if (!$rv && $cmd && $cmd ne 'log');

usage(0) if $_help;
version() if $_version;
usage(1) unless defined $cmd;
load_authors() if $_authors;

# make sure we're always running
unless ($cmd =~ /(?:clone|init|multi-init)$/) {
	unless (-d $ENV{GIT_DIR}) {
		if ($git_dir_user_set) {
			die "GIT_DIR=$ENV{GIT_DIR} explicitly set, ",
			    "but it is not a directory\n";
		}
		my $git_dir = delete $ENV{GIT_DIR};
		chomp(my $cdup = command_oneline(qw/rev-parse --show-cdup/));
		unless (length $cdup) {
			die "Already at toplevel, but $git_dir ",
			    "not found '$cdup'\n";
		}
		chdir $cdup or die "Unable to chdir up to '$cdup'\n";
		unless (-d $git_dir) {
			die "$git_dir still not found after going to ",
			    "'$cdup'\n";
		}
		$ENV{GIT_DIR} = $git_dir;
	}
}
unless ($cmd =~ /^(?:clone|init|multi-init|commit-diff)$/) {
	Git::SVN::Migration::migration_check();
}
Git::SVN::init_vars();
eval {
	Git::SVN::verify_remotes_sanity();
	$cmd{$cmd}->[0]->(@ARGV);
};
fatal $@ if $@;
post_fetch_checkout();
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
		next if /^multi-/; # don't show deprecated commands
		print $fd '  ',pack('A17',$_),$cmd{$_}->[1],"\n";
		foreach (keys %{$cmd{$_}->[2]}) {
			# mixed-case options are for .git/config only
			next if /[A-Z]/ && /^[a-z]+$/i;
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

sub do_git_init_db {
	unless (-d $ENV{GIT_DIR}) {
		my @init_db = ('init');
		push @init_db, "--template=$_template" if defined $_template;
		if (defined $_shared) {
			if ($_shared =~ /[a-z]/) {
				push @init_db, "--shared=$_shared";
			} else {
				push @init_db, "--shared";
			}
		}
		command_noisy(@init_db);
	}
	my $set;
	my $pfx = "svn-remote.$Git::SVN::default_repo_id";
	foreach my $i (keys %icv) {
		die "'$set' and '$i' cannot both be set\n" if $set;
		next unless defined $icv{$i};
		command_noisy('config', "$pfx.$i", $icv{$i});
		$set = $i;
	}
}

sub init_subdir {
	my $repo_path = shift or return;
	mkpath([$repo_path]) unless -d $repo_path;
	chdir $repo_path or die "Couldn't chdir to $repo_path: $!\n";
	$ENV{GIT_DIR} = '.git';
}

sub cmd_clone {
	my ($url, $path) = @_;
	if (!defined $path &&
	    (defined $_trunk || defined $_branches || defined $_tags) &&
	    $url !~ m#^[a-z\+]+://#) {
		$path = $url;
	}
	$path = basename($url) if !defined $path || !length $path;
	cmd_init($url, $path);
	Git::SVN::fetch_all($Git::SVN::default_repo_id);
}

sub cmd_init {
	if (defined $_trunk || defined $_branches || defined $_tags) {
		return cmd_multi_init(@_);
	}
	my $url = shift or die "SVN repository location required ",
	                       "as a command-line argument\n";
	init_subdir(@_);
	do_git_init_db();

	Git::SVN->init($url);
}

sub cmd_fetch {
	if (grep /^\d+=./, @_) {
		die "'<rev>=<commit>' fetch arguments are ",
		    "no longer supported.\n";
	}
	my ($remote) = @_;
	if (@_ > 1) {
		die "Usage: $0 fetch [--all] [svn-remote]\n";
	}
	$remote ||= $Git::SVN::default_repo_id;
	if ($_fetch_all) {
		cmd_multi_fetch();
	} else {
		Git::SVN::fetch_all($remote, Git::SVN::read_all_remotes());
	}
}

sub cmd_set_tree {
	my (@commits) = @_;
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
			fatal "Failed to rev-parse $c\n";
		}
	}
	my $gs = Git::SVN->new;
	my ($r_last, $cmt_last) = $gs->last_rev_commit;
	$gs->fetch;
	if (defined $gs->{last_rev} && $r_last != $gs->{last_rev}) {
		fatal "There are new revisions that were fetched ",
		      "and need to be merged (or acknowledged) ",
		      "before committing.\nlast rev: $r_last\n",
		      " current: $gs->{last_rev}\n";
	}
	$gs->set_tree($_) foreach @revs;
	print "Done committing ",scalar @revs," revisions to SVN\n";
}

sub cmd_dcommit {
	my $head = shift;
	$head ||= 'HEAD';
	my @refs;
	my ($url, $rev, $uuid, $gs) = working_head_info($head, \@refs);
	unless ($gs) {
		die "Unable to determine upstream SVN information from ",
		    "$head history\n";
	}
	my $c = $refs[-1];
	my $last_rev;
	foreach my $d (@refs) {
		if (!verify_ref("$d~1")) {
			fatal "Commit $d\n",
			      "has no parent commit, and therefore ",
			      "nothing to diff against.\n",
			      "You should be working from a repository ",
			      "originally created by git-svn\n";
		}
		unless (defined $last_rev) {
			(undef, $last_rev, undef) = cmt_metadata("$d~1");
			unless (defined $last_rev) {
				fatal "Unable to extract revision information ",
				      "from commit $d~1\n";
			}
		}
		if ($_dry_run) {
			print "diff-tree $d~1 $d\n";
		} else {
			my %ed_opts = ( r => $last_rev,
			                log => get_commit_entry($d)->{log},
			                ra => Git::SVN::Ra->new($gs->full_url),
			                tree_a => "$d~1",
			                tree_b => $d,
			                editor_cb => sub {
			                       print "Committed r$_[0]\n";
			                       $last_rev = $_[0]; },
			                svn_path => '');
			if (!SVN::Git::Editor->new(\%ed_opts)->apply_diff) {
				print "No changes\n$d~1 == $d\n";
			}
		}
	}
	return if $_dry_run;
	unless ($gs) {
		warn "Could not determine fetch information for $url\n",
		     "Will not attempt to fetch and rebase commits.\n",
		     "This probably means you have useSvmProps and should\n",
		     "now resync your SVN::Mirror repository.\n";
		return;
	}
	$_fetch_all ? $gs->fetch_all : $gs->fetch;
	unless ($_no_rebase) {
		# we always want to rebase against the current HEAD, not any
		# head that was passed to us
		my @diff = command('diff-tree', 'HEAD', $gs->refname, '--');
		my @finish;
		if (@diff) {
			@finish = rebase_cmd();
			print STDERR "W: HEAD and ", $gs->refname, " differ, ",
				     "using @finish:\n", "@diff";
		} else {
			print "No changes between current HEAD and ",
			      $gs->refname, "\nResetting to the latest ",
			      $gs->refname, "\n";
			@finish = qw/reset --mixed/;
		}
		command_noisy(@finish, $gs->refname);
	}
}

sub cmd_find_rev {
	my $revision_or_hash = shift;
	my $result;
	if ($revision_or_hash =~ /^r\d+$/) {
		my $head = shift;
		$head ||= 'HEAD';
		my @refs;
		my (undef, undef, undef, $gs) = working_head_info($head, \@refs);
		unless ($gs) {
			die "Unable to determine upstream SVN information from ",
			    "$head history\n";
		}
		my $desired_revision = substr($revision_or_hash, 1);
		$result = $gs->rev_db_get($desired_revision);
	} else {
		my (undef, $rev, undef) = cmt_metadata($revision_or_hash);
		$result = $rev;
	}
	print "$result\n" if $result;
}

sub cmd_rebase {
	command_noisy(qw/update-index --refresh/);
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	unless ($gs) {
		die "Unable to determine upstream SVN information from ",
		    "working tree history\n";
	}
	if (command(qw/diff-index HEAD --/)) {
		print STDERR "Cannot rebase with uncommited changes:\n";
		command_noisy('status');
		exit 1;
	}
	unless ($_local) {
		$_fetch_all ? $gs->fetch_all : $gs->fetch;
	}
	command_noisy(rebase_cmd(), $gs->refname);
}

sub cmd_show_ignore {
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	$gs ||= Git::SVN->new;
	my $r = (defined $_revision ? $_revision : $gs->ra->get_latest_revnum);
	$gs->traverse_ignore(\*STDOUT, $gs->{path}, $r);
}

sub cmd_multi_init {
	my $url = shift;
	unless (defined $_trunk || defined $_branches || defined $_tags) {
		usage(1);
	}

	# there are currently some bugs that prevent multi-init/multi-fetch
	# setups from working well without this.
	$Git::SVN::_minimize_url = 1;

	$_prefix = '' unless defined $_prefix;
	if (defined $url) {
		$url =~ s#/+$##;
		init_subdir(@_);
	}
	do_git_init_db();
	if (defined $_trunk) {
		my $trunk_ref = $_prefix . 'trunk';
		# try both old-style and new-style lookups:
		my $gs_trunk = eval { Git::SVN->new($trunk_ref) };
		unless ($gs_trunk) {
			my ($trunk_url, $trunk_path) =
			                      complete_svn_url($url, $_trunk);
			$gs_trunk = Git::SVN->init($trunk_url, $trunk_path,
						   undef, $trunk_ref);
		}
	}
	return unless defined $_branches || defined $_tags;
	my $ra = $url ? Git::SVN::Ra->new($url) : undef;
	complete_url_ls_init($ra, $_branches, '--branches/-b', $_prefix);
	complete_url_ls_init($ra, $_tags, '--tags/-t', $_prefix . 'tags/');
}

sub cmd_multi_fetch {
	my $remotes = Git::SVN::read_all_remotes();
	foreach my $repo_id (sort keys %$remotes) {
		if ($remotes->{$repo_id}->{url}) {
			Git::SVN::fetch_all($repo_id, $remotes);
		}
	}
}

# this command is special because it requires no metadata
sub cmd_commit_diff {
	my ($ta, $tb, $url) = @_;
	my $usage = "Usage: $0 commit-diff -r<revision> ".
	            "<tree-ish> <tree-ish> [<URL>]\n";
	fatal($usage) if (!defined $ta || !defined $tb);
	my $svn_path;
	if (!defined $url) {
		my $gs = eval { Git::SVN->new };
		if (!$gs) {
			fatal("Needed URL or usable git-svn --id in ",
			      "the command-line\n", $usage);
		}
		$url = $gs->{url};
		$svn_path = $gs->{path};
	}
	unless (defined $_revision) {
		fatal("-r|--revision is a required argument\n", $usage);
	}
	if (defined $_message && defined $_file) {
		fatal("Both --message/-m and --file/-F specified ",
		      "for the commit message.\n",
		      "I have no idea what you mean\n");
	}
	if (defined $_file) {
		$_message = file_to_s($_file);
	} else {
		$_message ||= get_commit_entry($tb)->{log};
	}
	my $ra ||= Git::SVN::Ra->new($url);
	$svn_path ||= $ra->{svn_path};
	my $r = $_revision;
	if ($r eq 'HEAD') {
		$r = $ra->get_latest_revnum;
	} elsif ($r !~ /^\d+$/) {
		die "revision argument: $r not understood by git-svn\n";
	}
	my %ed_opts = ( r => $r,
	                log => $_message,
	                ra => $ra,
	                tree_a => $ta,
	                tree_b => $tb,
	                editor_cb => sub { print "Committed r$_[0]\n" },
	                svn_path => $svn_path );
	if (!SVN::Git::Editor->new(\%ed_opts)->apply_diff) {
		print "No changes\n$ta == $tb\n";
	}
}

########################### utility functions #########################

sub rebase_cmd {
	my @cmd = qw/rebase/;
	push @cmd, '-v' if $_verbose;
	push @cmd, qw/--merge/ if $_merge;
	push @cmd, "--strategy=$_strategy" if $_strategy;
	@cmd;
}

sub post_fetch_checkout {
	return if $_no_checkout;
	my $gs = $Git::SVN::_head or return;
	return if verify_ref('refs/heads/master^0');

	my $valid_head = verify_ref('HEAD^0');
	command_noisy(qw(update-ref refs/heads/master), $gs->refname);
	return if ($valid_head || !verify_ref('HEAD^0'));

	return if $ENV{GIT_DIR} !~ m#^(?:.*/)?\.git$#;
	my $index = $ENV{GIT_INDEX_FILE} || "$ENV{GIT_DIR}/index";
	return if -f $index;

	chomp(my $bare = `git config --bool --get core.bare`);
	return if $bare eq 'true';
	return if command_oneline(qw/rev-parse --is-inside-git-dir/) eq 'true';
	command_noisy(qw/read-tree -m -u -v HEAD HEAD/);
	print STDERR "Checked out HEAD:\n  ",
	             $gs->full_url, " r", $gs->last_rev, "\n";
}

sub complete_svn_url {
	my ($url, $path) = @_;
	$path =~ s#/+$##;
	if ($path !~ m#^[a-z\+]+://#) {
		if (!defined $url || $url !~ m#^[a-z\+]+://#) {
			fatal("E: '$path' is not a complete URL ",
			      "and a separate URL is not specified\n");
		}
		return ($url, $path);
	}
	return ($path, '');
}

sub complete_url_ls_init {
	my ($ra, $repo_path, $switch, $pfx) = @_;
	unless ($repo_path) {
		print STDERR "W: $switch not specified\n";
		return;
	}
	$repo_path =~ s#/+$##;
	if ($repo_path =~ m#^[a-z\+]+://#) {
		$ra = Git::SVN::Ra->new($repo_path);
		$repo_path = '';
	} else {
		$repo_path =~ s#^/+##;
		unless ($ra) {
			fatal("E: '$repo_path' is not a complete URL ",
			      "and a separate URL is not specified\n");
		}
	}
	my $url = $ra->{url};
	my $gs = Git::SVN->init($url, undef, undef, undef, 1);
	my $k = "svn-remote.$gs->{repo_id}.url";
	my $orig_url = eval { command_oneline(qw/config --get/, $k) };
	if ($orig_url && ($orig_url ne $gs->{url})) {
		die "$k already set: $orig_url\n",
		    "wanted to set to: $gs->{url}\n";
	}
	command_oneline('config', $k, $gs->{url}) unless $orig_url;
	my $remote_path = "$ra->{svn_path}/$repo_path/*";
	$remote_path =~ s#/+#/#g;
	$remote_path =~ s#^/##g;
	my ($n) = ($switch =~ /^--(\w+)/);
	if (length $pfx && $pfx !~ m#/$#) {
		die "--prefix='$pfx' must have a trailing slash '/'\n";
	}
	command_noisy('config', "svn-remote.$gs->{repo_id}.$n",
				"$remote_path:refs/remotes/$pfx*");
}

sub verify_ref {
	my ($ref) = @_;
	eval { command_oneline([ 'rev-parse', '--verify', $ref ],
	                       { STDERR => 0 }); };
}

sub get_tree_from_treeish {
	my ($treeish) = @_;
	# $treeish can be a symbolic ref, too:
	my $type = command_oneline(qw/cat-file -t/, $treeish);
	my $expected;
	while ($type eq 'tag') {
		($treeish, $type) = command(qw/cat-file tag/, $treeish);
	}
	if ($type eq 'commit') {
		$expected = (grep /^tree /, command(qw/cat-file commit/,
		                                    $treeish))[0];
		($expected) = ($expected =~ /^tree ($sha1)$/o);
		die "Unable to get tree from $treeish\n" unless $expected;
	} elsif ($type eq 'tree') {
		$expected = $treeish;
	} else {
		die "$treeish is a $type, expected tree, tag or commit\n";
	}
	return $expected;
}

sub get_commit_entry {
	my ($treeish) = shift;
	my %log_entry = ( log => '', tree => get_tree_from_treeish($treeish) );
	my $commit_editmsg = "$ENV{GIT_DIR}/COMMIT_EDITMSG";
	my $commit_msg = "$ENV{GIT_DIR}/COMMIT_MSG";
	open my $log_fh, '>', $commit_editmsg or croak $!;

	my $type = command_oneline(qw/cat-file -t/, $treeish);
	if ($type eq 'commit' || $type eq 'tag') {
		my ($msg_fh, $ctx) = command_output_pipe('cat-file',
		                                         $type, $treeish);
		my $in_msg = 0;
		while (<$msg_fh>) {
			if (!$in_msg) {
				$in_msg = 1 if (/^\s*$/);
			} elsif (/^git-svn-id: /) {
				# skip this for now, we regenerate the
				# correct one on re-fetch anyways
				# TODO: set *:merge properties or like...
			} else {
				print $log_fh $_ or croak $!;
			}
		}
		command_close_pipe($msg_fh, $ctx);
	}
	close $log_fh or croak $!;

	if ($_edit || ($type eq 'tree')) {
		my $editor = $ENV{VISUAL} || $ENV{EDITOR} || 'vi';
		# TODO: strip out spaces, comments, like git-commit.sh
		system($editor, $commit_editmsg);
	}
	rename $commit_editmsg, $commit_msg or croak $!;
	open $log_fh, '<', $commit_msg or croak $!;
	{ local $/; chomp($log_entry{log} = <$log_fh>); }
	close $log_fh or croak $!;
	unlink $commit_msg;
	\%log_entry;
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

# '<svn username> = real-name <email address>' mapping based on git-svnimport:
sub load_authors {
	open my $authors, '<', $_authors or die "Can't open $_authors $!\n";
	my $log = $cmd eq 'log';
	while (<$authors>) {
		chomp;
		next unless /^(\S+?|\(no author\))\s*=\s*(.+?)\s*<(.+)>\s*$/;
		my ($user, $name, $email) = ($1, $2, $3);
		if ($log) {
			$Git::SVN::Log::rusers{"$name <$email>"} = $user;
		} else {
			$users{$user} = [$name, $email];
		}
	}
	close $authors or croak $!;
}

# convert GetOpt::Long specs for use by git-config
sub read_repo_config {
	return unless -d $ENV{GIT_DIR};
	my $opts = shift;
	my @config_only;
	foreach my $o (keys %$opts) {
		# if we have mixedCase and a long option-only, then
		# it's a config-only variable that we don't need for
		# the command-line.
		push @config_only, $o if ($o =~ /[A-Z]/ && $o =~ /^[a-z]+$/i);
		my $v = $opts->{$o};
		my ($key) = ($o =~ /^([a-zA-Z\-]+)/);
		$key =~ s/-//g;
		my $arg = 'git-config';
		$arg .= ' --int' if ($o =~ /[:=]i$/);
		$arg .= ' --bool' if ($o !~ /[:=][sfi]$/);
		if (ref $v eq 'ARRAY') {
			chomp(my @tmp = `$arg --get-all svn.$key`);
			@$v = @tmp if @tmp;
		} else {
			chomp(my $tmp = `$arg --get svn.$key`);
			if ($tmp && !($arg =~ / --bool/ && $tmp eq 'false')) {
				$$v = $tmp;
			}
		}
	}
	delete @$opts{@config_only} if @config_only;
}

sub extract_metadata {
	my $id = shift or return (undef, undef, undef);
	my ($url, $rev, $uuid) = ($id =~ /^\s*git-svn-id:\s+(.*)\@(\d+)
							\s([a-f\d\-]+)$/x);
	if (!defined $rev || !$uuid || !$url) {
		# some of the original repositories I made had
		# identifiers like this:
		($rev, $uuid) = ($id =~/^\s*git-svn-id:\s(\d+)\@([a-f\d\-]+)/);
	}
	return ($url, $rev, $uuid);
}

sub cmt_metadata {
	return extract_metadata((grep(/^git-svn-id: /,
		command(qw/cat-file commit/, shift)))[-1]);
}

sub working_head_info {
	my ($head, $refs) = @_;
	my ($fh, $ctx) = command_output_pipe('log', $head);
	my $hash;
	my %max;
	while (<$fh>) {
		if ( m{^commit ($::sha1)$} ) {
			unshift @$refs, $hash if $hash and $refs;
			$hash = $1;
			next;
		}
		next unless s{^\s*(git-svn-id:)}{$1};
		my ($url, $rev, $uuid) = extract_metadata($_);
		if (defined $url && defined $rev) {
			next if $max{$url} and $max{$url} < $rev;
			if (my $gs = Git::SVN->find_by_url($url)) {
				my $c = $gs->rev_db_get($rev);
				if ($c && $c eq $hash) {
					close $fh; # break the pipe
					return ($url, $rev, $uuid, $gs);
				} else {
					$max{$url} ||= $gs->rev_db_max;
				}
			}
		}
	}
	command_close_pipe($fh, $ctx);
	(undef, undef, undef, undef);
}

package Git::SVN;
use strict;
use warnings;
use vars qw/$default_repo_id $default_ref_id $_no_metadata $_follow_parent
            $_repack $_repack_flags $_use_svm_props $_head
            $_use_svnsync_props $no_reuse_existing $_minimize_url/;
use Carp qw/croak/;
use File::Path qw/mkpath/;
use File::Copy qw/copy/;
use IPC::Open3;

my $_repack_nr;
# properties that we do not log:
my %SKIP_PROP;
BEGIN {
	%SKIP_PROP = map { $_ => 1 } qw/svn:wc:ra_dav:version-url
	                                svn:special svn:executable
	                                svn:entry:committed-rev
	                                svn:entry:last-author
	                                svn:entry:uuid
	                                svn:entry:committed-date/;

	# some options are read globally, but can be overridden locally
	# per [svn-remote "..."] section.  Command-line options will *NOT*
	# override options set in an [svn-remote "..."] section
	no strict 'refs';
	for my $option (qw/follow_parent no_metadata use_svm_props
			   use_svnsync_props/) {
		my $key = $option;
		$key =~ tr/_//d;
		my $prop = "-$option";
		*$option = sub {
			my ($self) = @_;
			return $self->{$prop} if exists $self->{$prop};
			my $k = "svn-remote.$self->{repo_id}.$key";
			eval { command_oneline(qw/config --get/, $k) };
			if ($@) {
				$self->{$prop} = ${"Git::SVN::_$option"};
			} else {
				my $v = command_oneline(qw/config --bool/,$k);
				$self->{$prop} = $v eq 'false' ? 0 : 1;
			}
			return $self->{$prop};
		}
	}
}

my %LOCKFILES;
END { unlink keys %LOCKFILES if %LOCKFILES }

sub resolve_local_globs {
	my ($url, $fetch, $glob_spec) = @_;
	return unless defined $glob_spec;
	my $ref = $glob_spec->{ref};
	my $path = $glob_spec->{path};
	foreach (command(qw#for-each-ref --format=%(refname) refs/remotes#)) {
		next unless m#^refs/remotes/$ref->{regex}$#;
		my $p = $1;
		my $pathname = $path->full_path($p);
		my $refname = $ref->full_path($p);
		if (my $existing = $fetch->{$pathname}) {
			if ($existing ne $refname) {
				die "Refspec conflict:\n",
				    "existing: refs/remotes/$existing\n",
				    " globbed: refs/remotes/$refname\n";
			}
			my $u = (::cmt_metadata("refs/remotes/$refname"))[0];
			$u =~ s!^\Q$url\E(/|$)!! or die
			  "refs/remotes/$refname: '$url' not found in '$u'\n";
			if ($pathname ne $u) {
				warn "W: Refspec glob conflict ",
				     "(ref: refs/remotes/$refname):\n",
				     "expected path: $pathname\n",
				     "    real path: $u\n",
				     "Continuing ahead with $u\n";
				next;
			}
		} else {
			$fetch->{$pathname} = $refname;
		}
	}
}

sub parse_revision_argument {
	my ($base, $head) = @_;
	if (!defined $::_revision || $::_revision eq 'BASE:HEAD') {
		return ($base, $head);
	}
	return ($1, $2) if ($::_revision =~ /^(\d+):(\d+)$/);
	return ($::_revision, $::_revision) if ($::_revision =~ /^\d+$/);
	return ($head, $head) if ($::_revision eq 'HEAD');
	return ($base, $1) if ($::_revision =~ /^BASE:(\d+)$/);
	return ($1, $head) if ($::_revision =~ /^(\d+):HEAD$/);
	die "revision argument: $::_revision not understood by git-svn\n";
}

sub fetch_all {
	my ($repo_id, $remotes) = @_;
	if (ref $repo_id) {
		my $gs = $repo_id;
		$repo_id = undef;
		$repo_id = $gs->{repo_id};
	}
	$remotes ||= read_all_remotes();
	my $remote = $remotes->{$repo_id} or
	             die "[svn-remote \"$repo_id\"] unknown\n";
	my $fetch = $remote->{fetch};
	my $url = $remote->{url} or die "svn-remote.$repo_id.url not defined\n";
	my (@gs, @globs);
	my $ra = Git::SVN::Ra->new($url);
	my $uuid = $ra->get_uuid;
	my $head = $ra->get_latest_revnum;
	my $base = defined $fetch ? $head : 0;

	# read the max revs for wildcard expansion (branches/*, tags/*)
	foreach my $t (qw/branches tags/) {
		defined $remote->{$t} or next;
		push @globs, $remote->{$t};
		my $max_rev = eval { tmp_config(qw/--int --get/,
		                         "svn-remote.$repo_id.${t}-maxRev") };
		if (defined $max_rev && ($max_rev < $base)) {
			$base = $max_rev;
		} elsif (!defined $max_rev) {
			$base = 0;
		}
	}

	if ($fetch) {
		foreach my $p (sort keys %$fetch) {
			my $gs = Git::SVN->new($fetch->{$p}, $repo_id, $p);
			my $lr = $gs->rev_db_max;
			if (defined $lr) {
				$base = $lr if ($lr < $base);
			}
			push @gs, $gs;
		}
	}

	($base, $head) = parse_revision_argument($base, $head);
	$ra->gs_fetch_loop_common($base, $head, \@gs, \@globs);
}

sub read_all_remotes {
	my $r = {};
	foreach (grep { s/^svn-remote\.// } command(qw/config -l/)) {
		if (m!^(.+)\.fetch=\s*(.*)\s*:\s*refs/remotes/(.+)\s*$!) {
			$r->{$1}->{fetch}->{$2} = $3;
		} elsif (m!^(.+)\.url=\s*(.*)\s*$!) {
			$r->{$1}->{url} = $2;
		} elsif (m!^(.+)\.(branches|tags)=
		           (.*):refs/remotes/(.+)\s*$/!x) {
			my ($p, $g) = ($3, $4);
			my $rs = $r->{$1}->{$2} = {
			                  t => $2,
					  remote => $1,
			                  path => Git::SVN::GlobSpec->new($p),
			                  ref => Git::SVN::GlobSpec->new($g) };
			if (length($rs->{ref}->{right}) != 0) {
				die "The '*' glob character must be the last ",
				    "character of '$g'\n";
			}
		}
	}
	$r;
}

sub init_vars {
	if (defined $_repack) {
		$_repack = 1000 if ($_repack <= 0);
		$_repack_nr = $_repack;
		$_repack_flags ||= '-d';
	}
}

sub verify_remotes_sanity {
	return unless -d $ENV{GIT_DIR};
	my %seen;
	foreach (command(qw/config -l/)) {
		if (m!^svn-remote\.(?:.+)\.fetch=.*:refs/remotes/(\S+)\s*$!) {
			if ($seen{$1}) {
				die "Remote ref refs/remote/$1 is tracked by",
				    "\n  \"$_\"\nand\n  \"$seen{$1}\"\n",
				    "Please resolve this ambiguity in ",
				    "your git configuration file before ",
				    "continuing\n";
			}
			$seen{$1} = $_;
		}
	}
}

# we allow more chars than remotes2config.sh...
sub sanitize_remote_name {
	my ($name) = @_;
	$name =~ tr{A-Za-z0-9:,/+-}{.}c;
	$name;
}

sub find_existing_remote {
	my ($url, $remotes) = @_;
	return undef if $no_reuse_existing;
	my $existing;
	foreach my $repo_id (keys %$remotes) {
		my $u = $remotes->{$repo_id}->{url} or next;
		next if $u ne $url;
		$existing = $repo_id;
		last;
	}
	$existing;
}

sub init_remote_config {
	my ($self, $url, $no_write) = @_;
	$url =~ s!/+$!!; # strip trailing slash
	my $r = read_all_remotes();
	my $existing = find_existing_remote($url, $r);
	if ($existing) {
		unless ($no_write) {
			print STDERR "Using existing ",
				     "[svn-remote \"$existing\"]\n";
		}
		$self->{repo_id} = $existing;
	} elsif ($_minimize_url) {
		my $min_url = Git::SVN::Ra->new($url)->minimize_url;
		$existing = find_existing_remote($min_url, $r);
		if ($existing) {
			unless ($no_write) {
				print STDERR "Using existing ",
					     "[svn-remote \"$existing\"]\n";
			}
			$self->{repo_id} = $existing;
		}
		if ($min_url ne $url) {
			unless ($no_write) {
				print STDERR "Using higher level of URL: ",
					     "$url => $min_url\n";
			}
			my $old_path = $self->{path};
			$self->{path} = $url;
			$self->{path} =~ s!^\Q$min_url\E(/|$)!!;
			if (length $old_path) {
				$self->{path} .= "/$old_path";
			}
			$url = $min_url;
		}
	}
	my $orig_url;
	if (!$existing) {
		# verify that we aren't overwriting anything:
		$orig_url = eval {
			command_oneline('config', '--get',
					"svn-remote.$self->{repo_id}.url")
		};
		if ($orig_url && ($orig_url ne $url)) {
			die "svn-remote.$self->{repo_id}.url already set: ",
			    "$orig_url\nwanted to set to: $url\n";
		}
	}
	my ($xrepo_id, $xpath) = find_ref($self->refname);
	if (defined $xpath) {
		die "svn-remote.$xrepo_id.fetch already set to track ",
		    "$xpath:refs/remotes/", $self->refname, "\n";
	}
	unless ($no_write) {
		command_noisy('config',
			      "svn-remote.$self->{repo_id}.url", $url);
		command_noisy('config', '--add',
			      "svn-remote.$self->{repo_id}.fetch",
			      "$self->{path}:".$self->refname);
	}
	$self->{url} = $url;
}

sub find_by_url { # repos_root and, path are optional
	my ($class, $full_url, $repos_root, $path) = @_;

	return undef unless defined $full_url;
	remove_username($full_url);
	remove_username($repos_root) if defined $repos_root;
	my $remotes = read_all_remotes();
	if (defined $full_url && defined $repos_root && !defined $path) {
		$path = $full_url;
		$path =~ s#^\Q$repos_root\E(?:/|$)##;
	}
	foreach my $repo_id (keys %$remotes) {
		my $u = $remotes->{$repo_id}->{url} or next;
		remove_username($u);
		next if defined $repos_root && $repos_root ne $u;

		my $fetch = $remotes->{$repo_id}->{fetch} || {};
		foreach (qw/branches tags/) {
			resolve_local_globs($u, $fetch,
			                    $remotes->{$repo_id}->{$_});
		}
		my $p = $path;
		unless (defined $p) {
			$p = $full_url;
			$p =~ s#^\Q$u\E(?:/|$)## or next;
		}
		foreach my $f (keys %$fetch) {
			next if $f ne $p;
			return Git::SVN->new($fetch->{$f}, $repo_id, $f);
		}
	}
	undef;
}

sub init {
	my ($class, $url, $path, $repo_id, $ref_id, $no_write) = @_;
	my $self = _new($class, $repo_id, $ref_id, $path);
	if (defined $url) {
		$self->init_remote_config($url, $no_write);
	}
	$self;
}

sub find_ref {
	my ($ref_id) = @_;
	foreach (command(qw/config -l/)) {
		next unless m!^svn-remote\.(.+)\.fetch=
		              \s*(.*)\s*:\s*refs/remotes/(.+)\s*$!x;
		my ($repo_id, $path, $ref) = ($1, $2, $3);
		if ($ref eq $ref_id) {
			$path = '' if ($path =~ m#^\./?#);
			return ($repo_id, $path);
		}
	}
	(undef, undef, undef);
}

sub new {
	my ($class, $ref_id, $repo_id, $path) = @_;
	if (defined $ref_id && !defined $repo_id && !defined $path) {
		($repo_id, $path) = find_ref($ref_id);
		if (!defined $repo_id) {
			die "Could not find a \"svn-remote.*.fetch\" key ",
			    "in the repository configuration matching: ",
			    "refs/remotes/$ref_id\n";
		}
	}
	my $self = _new($class, $repo_id, $ref_id, $path);
	if (!defined $self->{path} || !length $self->{path}) {
		my $fetch = command_oneline('config', '--get',
		                            "svn-remote.$repo_id.fetch",
		                            ":refs/remotes/$ref_id\$") or
		     die "Failed to read \"svn-remote.$repo_id.fetch\" ",
		         "\":refs/remotes/$ref_id\$\" in config\n";
		($self->{path}, undef) = split(/\s*:\s*/, $fetch);
	}
	$self->{url} = command_oneline('config', '--get',
	                               "svn-remote.$repo_id.url") or
                  die "Failed to read \"svn-remote.$repo_id.url\" in config\n";
	$self->rebuild;
	$self;
}

sub refname { "refs/remotes/$_[0]->{ref_id}" }

sub svm_uuid {
	my ($self) = @_;
	return $self->{svm}->{uuid} if $self->svm;
	$self->ra;
	unless ($self->{svm}) {
		die "SVM UUID not cached, and reading remotely failed\n";
	}
	$self->{svm}->{uuid};
}

sub svm {
	my ($self) = @_;
	return $self->{svm} if $self->{svm};
	my $svm;
	# see if we have it in our config, first:
	eval {
		my $section = "svn-remote.$self->{repo_id}";
		$svm = {
		  source => tmp_config('--get', "$section.svm-source"),
		  uuid => tmp_config('--get', "$section.svm-uuid"),
		  replace => tmp_config('--get', "$section.svm-replace"),
		}
	};
	if ($svm && $svm->{source} && $svm->{uuid} && $svm->{replace}) {
		$self->{svm} = $svm;
	}
	$self->{svm};
}

sub _set_svm_vars {
	my ($self, $ra) = @_;
	return $ra if $self->svm;

	my @err = ( "useSvmProps set, but failed to read SVM properties\n",
		    "(svm:source, svm:uuid) ",
		    "from the following URLs:\n" );
	sub read_svm_props {
		my ($self, $ra, $path, $r) = @_;
		my $props = ($ra->get_dir($path, $r))[2];
		my $src = $props->{'svm:source'};
		my $uuid = $props->{'svm:uuid'};
		return undef if (!$src || !$uuid);

		chomp($src, $uuid);

		$uuid =~ m{^[0-9a-f\-]{30,}$}
		    or die "doesn't look right - svm:uuid is '$uuid'\n";

		# the '!' is used to mark the repos_root!/relative/path
		$src =~ s{/?!/?}{/};
		$src =~ s{/+$}{}; # no trailing slashes please
		# username is of no interest
		$src =~ s{(^[a-z\+]*://)[^/@]*@}{$1};

		my $replace = $ra->{url};
		$replace .= "/$path" if length $path;

		my $section = "svn-remote.$self->{repo_id}";
		tmp_config("$section.svm-source", $src);
		tmp_config("$section.svm-replace", $replace);
		tmp_config("$section.svm-uuid", $uuid);
		$self->{svm} = {
			source => $src,
			uuid => $uuid,
			replace => $replace
		};
	}

	my $r = $ra->get_latest_revnum;
	my $path = $self->{path};
	my %tried;
	while (length $path) {
		unless ($tried{"$self->{url}/$path"}) {
			return $ra if $self->read_svm_props($ra, $path, $r);
			$tried{"$self->{url}/$path"} = 1;
		}
		$path =~ s#/?[^/]+$##;
	}
	die "Path: '$path' should be ''\n" if $path ne '';
	return $ra if $self->read_svm_props($ra, $path, $r);
	$tried{"$self->{url}/$path"} = 1;

	if ($ra->{repos_root} eq $self->{url}) {
		die @err, (map { "  $_\n" } keys %tried), "\n";
	}

	# nope, make sure we're connected to the repository root:
	my $ok;
	my @tried_b;
	$path = $ra->{svn_path};
	$ra = Git::SVN::Ra->new($ra->{repos_root});
	while (length $path) {
		unless ($tried{"$ra->{url}/$path"}) {
			$ok = $self->read_svm_props($ra, $path, $r);
			last if $ok;
			$tried{"$ra->{url}/$path"} = 1;
		}
		$path =~ s#/?[^/]+$##;
	}
	die "Path: '$path' should be ''\n" if $path ne '';
	$ok ||= $self->read_svm_props($ra, $path, $r);
	$tried{"$ra->{url}/$path"} = 1;
	if (!$ok) {
		die @err, (map { "  $_\n" } keys %tried), "\n";
	}
	Git::SVN::Ra->new($self->{url});
}

sub svnsync {
	my ($self) = @_;
	return $self->{svnsync} if $self->{svnsync};

	if ($self->no_metadata) {
		die "Can't have both 'noMetadata' and ",
		    "'useSvnsyncProps' options set!\n";
	}
	if ($self->rewrite_root) {
		die "Can't have both 'useSvnsyncProps' and 'rewriteRoot' ",
		    "options set!\n";
	}

	my $svnsync;
	# see if we have it in our config, first:
	eval {
		my $section = "svn-remote.$self->{repo_id}";
		$svnsync = {
		  url => tmp_config('--get', "$section.svnsync-url"),
		  uuid => tmp_config('--get', "$section.svnsync-uuid"),
		}
	};
	if ($svnsync && $svnsync->{url} && $svnsync->{uuid}) {
		return $self->{svnsync} = $svnsync;
	}

	my $err = "useSvnsyncProps set, but failed to read " .
	          "svnsync property: svn:sync-from-";
	my $rp = $self->ra->rev_proplist(0);

	my $url = $rp->{'svn:sync-from-url'} or die $err . "url\n";
	$url =~ m{^[a-z\+]+://} or
	           die "doesn't look right - svn:sync-from-url is '$url'\n";

	my $uuid = $rp->{'svn:sync-from-uuid'} or die $err . "uuid\n";
	$uuid =~ m{^[0-9a-f\-]{30,}$} or
	           die "doesn't look right - svn:sync-from-uuid is '$uuid'\n";

	my $section = "svn-remote.$self->{repo_id}";
	tmp_config('--add', "$section.svnsync-uuid", $uuid);
	tmp_config('--add', "$section.svnsync-url", $url);
	return $self->{svnsync} = { url => $url, uuid => $uuid };
}

# this allows us to memoize our SVN::Ra UUID locally and avoid a
# remote lookup (useful for 'git svn log').
sub ra_uuid {
	my ($self) = @_;
	unless ($self->{ra_uuid}) {
		my $key = "svn-remote.$self->{repo_id}.uuid";
		my $uuid = eval { tmp_config('--get', $key) };
		if (!$@ && $uuid && $uuid =~ /^([a-f\d\-]{30,})$/) {
			$self->{ra_uuid} = $uuid;
		} else {
			die "ra_uuid called without URL\n" unless $self->{url};
			$self->{ra_uuid} = $self->ra->get_uuid;
			tmp_config('--add', $key, $self->{ra_uuid});
		}
	}
	$self->{ra_uuid};
}

sub ra {
	my ($self) = shift;
	my $ra = Git::SVN::Ra->new($self->{url});
	if ($self->use_svm_props && !$self->{svm}) {
		if ($self->no_metadata) {
			die "Can't have both 'noMetadata' and ",
			    "'useSvmProps' options set!\n";
		} elsif ($self->use_svnsync_props) {
			die "Can't have both 'useSvnsyncProps' and ",
			    "'useSvmProps' options set!\n";
		}
		$ra = $self->_set_svm_vars($ra);
		$self->{-want_revprops} = 1;
	}
	$ra;
}

sub rel_path {
	my ($self) = @_;
	my $repos_root = $self->ra->{repos_root};
	return $self->{path} if ($self->{url} eq $repos_root);
	my $url = $self->{url} .
	          (length $self->{path} ? "/$self->{path}" : $self->{path});
	$url =~ s!^\Q$repos_root\E(?:/+|$)!!g;
	$url;
}

sub traverse_ignore {
	my ($self, $fh, $path, $r) = @_;
	$path =~ s#^/+##g;
	my $ra = $self->ra;
	my ($dirent, undef, $props) = $ra->get_dir($path, $r);
	my $p = $path;
	$p =~ s#^\Q$self->{path}\E(/|$)##;
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
		next if $dirent->{$_}->{kind} != $SVN::Node::dir;
		$self->traverse_ignore($fh, "$path/$_", $r);
	}
}

sub last_rev { ($_[0]->last_rev_commit)[0] }
sub last_commit { ($_[0]->last_rev_commit)[1] }

# returns the newest SVN revision number and newest commit SHA1
sub last_rev_commit {
	my ($self) = @_;
	if (defined $self->{last_rev} && defined $self->{last_commit}) {
		return ($self->{last_rev}, $self->{last_commit});
	}
	my $c = ::verify_ref($self->refname.'^0');
	if ($c && !$self->use_svm_props && !$self->no_metadata) {
		my $rev = (::cmt_metadata($c))[1];
		if (defined $rev) {
			($self->{last_rev}, $self->{last_commit}) = ($rev, $c);
			return ($rev, $c);
		}
	}
	my $db_path = $self->db_path;
	unless (-e $db_path) {
		($self->{last_rev}, $self->{last_commit}) = (undef, undef);
		return (undef, undef);
	}
	my $offset = -41; # from tail
	my $rl;
	open my $fh, '<', $db_path or croak "$db_path not readable: $!\n";
	sysseek($fh, $offset, 2); # don't care for errors
	sysread($fh, $rl, 41) == 41 or return (undef, undef);
	chomp $rl;
	while (('0' x40) eq $rl && sysseek($fh, 0, 1) != 0) {
		$offset -= 41;
		sysseek($fh, $offset, 2); # don't care for errors
		sysread($fh, $rl, 41) == 41 or return (undef, undef);
		chomp $rl;
	}
	if ($c && $c ne $rl) {
		die "$db_path and ", $self->refname,
		    " inconsistent!:\n$c != $rl\n";
	}
	my $rev = sysseek($fh, 0, 1) or croak $!;
	$rev =  ($rev - 41) / 41;
	close $fh or croak $!;
	($self->{last_rev}, $self->{last_commit}) = ($rev, $c);
	return ($rev, $c);
}

sub get_fetch_range {
	my ($self, $min, $max) = @_;
	$max ||= $self->ra->get_latest_revnum;
	$min ||= $self->rev_db_max;
	(++$min, $max);
}

sub tmp_config {
	my (@args) = @_;
	my $old_def_config = "$ENV{GIT_DIR}/svn/config";
	my $config = "$ENV{GIT_DIR}/svn/.metadata";
	if (! -f $config && -f $old_def_config) {
		rename $old_def_config, $config or
		       die "Failed rename $old_def_config => $config: $!\n";
	}
	my $old_config = $ENV{GIT_CONFIG};
	$ENV{GIT_CONFIG} = $config;
	$@ = undef;
	my @ret = eval {
		unless (-f $config) {
			mkfile($config);
			open my $fh, '>', $config or
			    die "Can't open $config: $!\n";
			print $fh "; This file is used internally by ",
			          "git-svn\n" or die
				  "Couldn't write to $config: $!\n";
			print $fh "; You should not have to edit it\n" or
			      die "Couldn't write to $config: $!\n";
			close $fh or die "Couldn't close $config: $!\n";
		}
		command('config', @args);
	};
	my $err = $@;
	if (defined $old_config) {
		$ENV{GIT_CONFIG} = $old_config;
	} else {
		delete $ENV{GIT_CONFIG};
	}
	die $err if $err;
	wantarray ? @ret : $ret[0];
}

sub tmp_index_do {
	my ($self, $sub) = @_;
	my $old_index = $ENV{GIT_INDEX_FILE};
	$ENV{GIT_INDEX_FILE} = $self->{index};
	$@ = undef;
	my @ret = eval {
		my ($dir, $base) = ($self->{index} =~ m#^(.*?)/?([^/]+)$#);
		mkpath([$dir]) unless -d $dir;
		&$sub;
	};
	my $err = $@;
	if (defined $old_index) {
		$ENV{GIT_INDEX_FILE} = $old_index;
	} else {
		delete $ENV{GIT_INDEX_FILE};
	}
	die $err if $err;
	wantarray ? @ret : $ret[0];
}

sub assert_index_clean {
	my ($self, $treeish) = @_;

	$self->tmp_index_do(sub {
		command_noisy('read-tree', $treeish) unless -e $self->{index};
		my $x = command_oneline('write-tree');
		my ($y) = (command(qw/cat-file commit/, $treeish) =~
		           /^tree ($::sha1)/mo);
		return if $y eq $x;

		warn "Index mismatch: $y != $x\nrereading $treeish\n";
		unlink $self->{index} or die "unlink $self->{index}: $!\n";
		command_noisy('read-tree', $treeish);
		$x = command_oneline('write-tree');
		if ($y ne $x) {
			::fatal "trees ($treeish) $y != $x\n",
			        "Something is seriously wrong...\n";
		}
	});
}

sub get_commit_parents {
	my ($self, $log_entry) = @_;
	my (%seen, @ret, @tmp);
	# legacy support for 'set-tree'; this is only used by set_tree_cb:
	if (my $ip = $self->{inject_parents}) {
		if (my $commit = delete $ip->{$log_entry->{revision}}) {
			push @tmp, $commit;
		}
	}
	if (my $cur = ::verify_ref($self->refname.'^0')) {
		push @tmp, $cur;
	}
	push @tmp, $_ foreach (@{$log_entry->{parents}}, @tmp);
	while (my $p = shift @tmp) {
		next if $seen{$p};
		$seen{$p} = 1;
		push @ret, $p;
		# MAXPARENT is defined to 16 in commit-tree.c:
		last if @ret >= 16;
	}
	if (@tmp) {
		die "r$log_entry->{revision}: No room for parents:\n\t",
		    join("\n\t", @tmp), "\n";
	}
	@ret;
}

sub rewrite_root {
	my ($self) = @_;
	return $self->{-rewrite_root} if exists $self->{-rewrite_root};
	my $k = "svn-remote.$self->{repo_id}.rewriteRoot";
	my $rwr = eval { command_oneline(qw/config --get/, $k) };
	if ($rwr) {
		$rwr =~ s#/+$##;
		if ($rwr !~ m#^[a-z\+]+://#) {
			die "$rwr is not a valid URL (key: $k)\n";
		}
	}
	$self->{-rewrite_root} = $rwr;
}

sub metadata_url {
	my ($self) = @_;
	($self->rewrite_root || $self->{url}) .
	   (length $self->{path} ? '/' . $self->{path} : '');
}

sub full_url {
	my ($self) = @_;
	$self->{url} . (length $self->{path} ? '/' . $self->{path} : '');
}

sub do_git_commit {
	my ($self, $log_entry) = @_;
	my $lr = $self->last_rev;
	if (defined $lr && $lr >= $log_entry->{revision}) {
		die "Last fetched revision of ", $self->refname,
		    " was r$lr, but we are about to fetch: ",
		    "r$log_entry->{revision}!\n";
	}
	if (my $c = $self->rev_db_get($log_entry->{revision})) {
		croak "$log_entry->{revision} = $c already exists! ",
		      "Why are we refetching it?\n";
	}
	$ENV{GIT_AUTHOR_NAME} = $ENV{GIT_COMMITTER_NAME} = $log_entry->{name};
	$ENV{GIT_AUTHOR_EMAIL} = $ENV{GIT_COMMITTER_EMAIL} =
	                                                  $log_entry->{email};
	$ENV{GIT_AUTHOR_DATE} = $ENV{GIT_COMMITTER_DATE} = $log_entry->{date};

	my $tree = $log_entry->{tree};
	if (!defined $tree) {
		$tree = $self->tmp_index_do(sub {
		                            command_oneline('write-tree') });
	}
	die "Tree is not a valid sha1: $tree\n" if $tree !~ /^$::sha1$/o;

	my @exec = ('git-commit-tree', $tree);
	foreach ($self->get_commit_parents($log_entry)) {
		push @exec, '-p', $_;
	}
	defined(my $pid = open3(my $msg_fh, my $out_fh, '>&STDERR', @exec))
	                                                           or croak $!;
	print $msg_fh $log_entry->{log} or croak $!;
	unless ($self->no_metadata) {
		print $msg_fh "\ngit-svn-id: $log_entry->{metadata}\n"
		              or croak $!;
	}
	$msg_fh->flush == 0 or croak $!;
	close $msg_fh or croak $!;
	chomp(my $commit = do { local $/; <$out_fh> });
	close $out_fh or croak $!;
	waitpid $pid, 0;
	croak $? if $?;
	if ($commit !~ /^$::sha1$/o) {
		die "Failed to commit, invalid sha1: $commit\n";
	}

	$self->rev_db_set($log_entry->{revision}, $commit, 1);

	$self->{last_rev} = $log_entry->{revision};
	$self->{last_commit} = $commit;
	print "r$log_entry->{revision}";
	if (defined $log_entry->{svm_revision}) {
		 print " (\@$log_entry->{svm_revision})";
		 $self->rev_db_set($log_entry->{svm_revision}, $commit,
		                   0, $self->svm_uuid);
	}
	print " = $commit ($self->{ref_id})\n";
	if (defined $_repack && (--$_repack_nr == 0)) {
		$_repack_nr = $_repack;
		# repack doesn't use any arguments with spaces in them, does it?
		print "Running git repack $_repack_flags ...\n";
		command_noisy('repack', split(/\s+/, $_repack_flags));
		print "Done repacking\n";
	}
	return $commit;
}

sub match_paths {
	my ($self, $paths, $r) = @_;
	return 1 if $self->{path} eq '';
	if (my $path = $paths->{"/$self->{path}"}) {
		return ($path->{action} eq 'D') ? 0 : 1;
	}
	$self->{path_regex} ||= qr/^\/\Q$self->{path}\E\//;
	if (grep /$self->{path_regex}/, keys %$paths) {
		return 1;
	}
	my $c = '';
	foreach (split m#/#, $self->{path}) {
		$c .= "/$_";
		next unless ($paths->{$c} &&
		             ($paths->{$c}->{action} =~ /^[AR]$/));
		if ($self->ra->check_path($self->{path}, $r) ==
		    $SVN::Node::dir) {
			return 1;
		}
	}
	return 0;
}

sub find_parent_branch {
	my ($self, $paths, $rev) = @_;
	return undef unless $self->follow_parent;
	unless (defined $paths) {
		my $err_handler = $SVN::Error::handler;
		$SVN::Error::handler = \&Git::SVN::Ra::skip_unknown_revs;
		$self->ra->get_log([$self->{path}], $rev, $rev, 0, 1, 1, sub {
		                   $paths =
				      Git::SVN::Ra::dup_changed_paths($_[0]) });
		$SVN::Error::handler = $err_handler;
	}
	return undef unless defined $paths;

	# look for a parent from another branch:
	my @b_path_components = split m#/#, $self->rel_path;
	my @a_path_components;
	my $i;
	while (@b_path_components) {
		$i = $paths->{'/'.join('/', @b_path_components)};
		last if $i && defined $i->{copyfrom_path};
		unshift(@a_path_components, pop(@b_path_components));
	}
	return undef unless defined $i && defined $i->{copyfrom_path};
	my $branch_from = $i->{copyfrom_path};
	if (@a_path_components) {
		print STDERR "branch_from: $branch_from => ";
		$branch_from .= '/'.join('/', @a_path_components);
		print STDERR $branch_from, "\n";
	}
	my $r = $i->{copyfrom_rev};
	my $repos_root = $self->ra->{repos_root};
	my $url = $self->ra->{url};
	my $new_url = $repos_root . $branch_from;
	print STDERR  "Found possible branch point: ",
	              "$new_url => ", $self->full_url, ", $r\n";
	$branch_from =~ s#^/##;
	my $gs = Git::SVN->find_by_url($new_url, $repos_root, $branch_from);
	unless ($gs) {
		my $ref_id = $self->{ref_id};
		$ref_id =~ s/\@\d+$//;
		$ref_id .= "\@$r";
		# just grow a tail if we're not unique enough :x
		$ref_id .= '-' while find_ref($ref_id);
		print STDERR "Initializing parent: $ref_id\n";
		$gs = Git::SVN->init($new_url, '', $ref_id, $ref_id, 1);
	}
	my ($r0, $parent) = $gs->find_rev_before($r, 1);
	if (!defined $r0 || !defined $parent) {
		my ($base, $head) = parse_revision_argument(0, $r);
		if ($base <= $r) {
			$gs->fetch($base, $r);
		}
		($r0, $parent) = $gs->last_rev_commit;
	}
	if (defined $r0 && defined $parent) {
		print STDERR "Found branch parent: ($self->{ref_id}) $parent\n";
		my $ed;
		if ($self->ra->can_do_switch) {
			$self->assert_index_clean($parent);
			print STDERR "Following parent with do_switch\n";
			# do_switch works with svn/trunk >= r22312, but that
			# is not included with SVN 1.4.3 (the latest version
			# at the moment), so we can't rely on it
			$self->{last_commit} = $parent;
			$ed = SVN::Git::Fetcher->new($self);
			$gs->ra->gs_do_switch($r0, $rev, $gs,
					      $self->full_url, $ed)
			  or die "SVN connection failed somewhere...\n";
		} else {
			print STDERR "Following parent with do_update\n";
			$ed = SVN::Git::Fetcher->new($self);
			$self->ra->gs_do_update($rev, $rev, $self, $ed)
			  or die "SVN connection failed somewhere...\n";
		}
		print STDERR "Successfully followed parent\n";
		return $self->make_log_entry($rev, [$parent], $ed);
	}
	return undef;
}

sub do_fetch {
	my ($self, $paths, $rev) = @_;
	my $ed;
	my ($last_rev, @parents);
	if (my $lc = $self->last_commit) {
		# we can have a branch that was deleted, then re-added
		# under the same name but copied from another path, in
		# which case we'll have multiple parents (we don't
		# want to break the original ref, nor lose copypath info):
		if (my $log_entry = $self->find_parent_branch($paths, $rev)) {
			push @{$log_entry->{parents}}, $lc;
			return $log_entry;
		}
		$ed = SVN::Git::Fetcher->new($self);
		$last_rev = $self->{last_rev};
		$ed->{c} = $lc;
		@parents = ($lc);
	} else {
		$last_rev = $rev;
		if (my $log_entry = $self->find_parent_branch($paths, $rev)) {
			return $log_entry;
		}
		$ed = SVN::Git::Fetcher->new($self);
	}
	unless ($self->ra->gs_do_update($last_rev, $rev, $self, $ed)) {
		die "SVN connection failed somewhere...\n";
	}
	$self->make_log_entry($rev, \@parents, $ed);
}

sub get_untracked {
	my ($self, $ed) = @_;
	my @out;
	my $h = $ed->{empty};
	foreach (sort keys %$h) {
		my $act = $h->{$_} ? '+empty_dir' : '-empty_dir';
		push @out, "  $act: " . uri_encode($_);
		warn "W: $act: $_\n";
	}
	foreach my $t (qw/dir_prop file_prop/) {
		$h = $ed->{$t} or next;
		foreach my $path (sort keys %$h) {
			my $ppath = $path eq '' ? '.' : $path;
			foreach my $prop (sort keys %{$h->{$path}}) {
				next if $SKIP_PROP{$prop};
				my $v = $h->{$path}->{$prop};
				my $t_ppath_prop = "$t: " .
				                    uri_encode($ppath) . ' ' .
				                    uri_encode($prop);
				if (defined $v) {
					push @out, "  +$t_ppath_prop " .
					           uri_encode($v);
				} else {
					push @out, "  -$t_ppath_prop";
				}
			}
		}
	}
	foreach my $t (qw/absent_file absent_directory/) {
		$h = $ed->{$t} or next;
		foreach my $parent (sort keys %$h) {
			foreach my $path (sort @{$h->{$parent}}) {
				push @out, "  $t: " .
				           uri_encode("$parent/$path");
				warn "W: $t: $parent/$path ",
				     "Insufficient permissions?\n";
			}
		}
	}
	\@out;
}

sub parse_svn_date {
	my $date = shift || return '+0000 1970-01-01 00:00:00';
	my ($Y,$m,$d,$H,$M,$S) = ($date =~ /^(\d{4})\-(\d\d)\-(\d\d)T
	                                    (\d\d)\:(\d\d)\:(\d\d).\d+Z$/x) or
	                                 croak "Unable to parse date: $date\n";
	"+0000 $Y-$m-$d $H:$M:$S";
}

sub check_author {
	my ($author) = @_;
	if (!defined $author || length $author == 0) {
		$author = '(no author)';
	}
	if (defined $::_authors && ! defined $::users{$author}) {
		die "Author: $author not defined in $::_authors file\n";
	}
	$author;
}

sub make_log_entry {
	my ($self, $rev, $parents, $ed) = @_;
	my $untracked = $self->get_untracked($ed);

	open my $un, '>>', "$self->{dir}/unhandled.log" or croak $!;
	print $un "r$rev\n" or croak $!;
	print $un $_, "\n" foreach @$untracked;
	my %log_entry = ( parents => $parents || [], revision => $rev,
	                  log => '');

	my $headrev;
	my $logged = delete $self->{logged_rev_props};
	if (!$logged || $self->{-want_revprops}) {
		my $rp = $self->ra->rev_proplist($rev);
		foreach (sort keys %$rp) {
			my $v = $rp->{$_};
			if (/^svn:(author|date|log)$/) {
				$log_entry{$1} = $v;
			} elsif ($_ eq 'svm:headrev') {
				$headrev = $v;
			} else {
				print $un "  rev_prop: ", uri_encode($_), ' ',
					  uri_encode($v), "\n";
			}
		}
	} else {
		map { $log_entry{$_} = $logged->{$_} } keys %$logged;
	}
	close $un or croak $!;

	$log_entry{date} = parse_svn_date($log_entry{date});
	$log_entry{log} .= "\n";
	my $author = $log_entry{author} = check_author($log_entry{author});
	my ($name, $email) = defined $::users{$author} ? @{$::users{$author}}
	                                               : ($author, undef);
	if (defined $headrev && $self->use_svm_props) {
		if ($self->rewrite_root) {
			die "Can't have both 'useSvmProps' and 'rewriteRoot' ",
			    "options set!\n";
		}
		my ($uuid, $r) = $headrev =~ m{^([a-f\d\-]{30,}):(\d+)$};
		# we don't want "SVM: initializing mirror for junk" ...
		return undef if $r == 0;
		my $svm = $self->svm;
		if ($uuid ne $svm->{uuid}) {
			die "UUID mismatch on SVM path:\n",
			    "expected: $svm->{uuid}\n",
			    "     got: $uuid\n";
		}
		my $full_url = $self->full_url;
		$full_url =~ s#^\Q$svm->{replace}\E(/|$)#$svm->{source}$1# or
		             die "Failed to replace '$svm->{replace}' with ",
		                 "'$svm->{source}' in $full_url\n";
		# throw away username for storing in records
		remove_username($full_url);
		$log_entry{metadata} = "$full_url\@$r $uuid";
		$log_entry{svm_revision} = $r;
		$email ||= "$author\@$uuid"
	} elsif ($self->use_svnsync_props) {
		my $full_url = $self->svnsync->{url};
		$full_url .= "/$self->{path}" if length $self->{path};
		remove_username($full_url);
		my $uuid = $self->svnsync->{uuid};
		$log_entry{metadata} = "$full_url\@$rev $uuid";
		$email ||= "$author\@$uuid"
	} else {
		my $url = $self->metadata_url;
		remove_username($url);
		$log_entry{metadata} = "$url\@$rev " .
		                       $self->ra->get_uuid;
		$email ||= "$author\@" . $self->ra->get_uuid;
	}
	$log_entry{name} = $name;
	$log_entry{email} = $email;
	\%log_entry;
}

sub fetch {
	my ($self, $min_rev, $max_rev, @parents) = @_;
	my ($last_rev, $last_commit) = $self->last_rev_commit;
	my ($base, $head) = $self->get_fetch_range($min_rev, $max_rev);
	$self->ra->gs_fetch_loop_common($base, $head, [$self]);
}

sub set_tree_cb {
	my ($self, $log_entry, $tree, $rev, $date, $author) = @_;
	$self->{inject_parents} = { $rev => $tree };
	$self->fetch(undef, undef);
}

sub set_tree {
	my ($self, $tree) = (shift, shift);
	my $log_entry = ::get_commit_entry($tree);
	unless ($self->{last_rev}) {
		fatal("Must have an existing revision to commit\n");
	}
	my %ed_opts = ( r => $self->{last_rev},
	                log => $log_entry->{log},
	                ra => $self->ra,
	                tree_a => $self->{last_commit},
	                tree_b => $tree,
	                editor_cb => sub {
			       $self->set_tree_cb($log_entry, $tree, @_) },
	                svn_path => $self->{path} );
	if (!SVN::Git::Editor->new(\%ed_opts)->apply_diff) {
		print "No changes\nr$self->{last_rev} = $tree\n";
	}
}

sub rebuild {
	my ($self) = @_;
	my $db_path = $self->db_path;
	return if (-e $db_path && ! -z $db_path);
	return unless ::verify_ref($self->refname.'^0');
	if (-f $self->{db_root}) {
		rename $self->{db_root}, $db_path or die
		     "rename $self->{db_root} => $db_path failed: $!\n";
		my ($dir, $base) = ($db_path =~ m#^(.*?)/?([^/]+)$#);
		symlink $base, $self->{db_root} or die
		     "symlink $base => $self->{db_root} failed: $!\n";
		return;
	}
	print "Rebuilding $db_path ...\n";
	my ($log, $ctx) = command_output_pipe("log", $self->refname);
	my $latest;
	my $full_url = $self->full_url;
	remove_username($full_url);
	my $svn_uuid;
	my $c;
	while (<$log>) {
		if ( m{^commit ($::sha1)$} ) {
			$c = $1;
			next;
		}
		next unless s{^\s*(git-svn-id:)}{$1};
		my ($url, $rev, $uuid) = ::extract_metadata($_);
		remove_username($url);

		# ignore merges (from set-tree)
		next if (!defined $rev || !$uuid);

		# if we merged or otherwise started elsewhere, this is
		# how we break out of it
		if ((defined $svn_uuid && ($uuid ne $svn_uuid)) ||
		    ($full_url && $url && ($url ne $full_url))) {
			next;
		}
		$latest ||= $rev;
		$svn_uuid ||= $uuid;

		$self->rev_db_set($rev, $c);
		print "r$rev = $c\n";
	}
	command_close_pipe($log, $ctx);
	print "Done rebuilding $db_path\n";
}

# rev_db:
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
# These files are disposable unless noMetadata or useSvmProps is set

sub _rev_db_set {
	my ($fh, $rev, $commit) = @_;
	my $offset = $rev * 41;
	# assume that append is the common case:
	seek $fh, 0, 2 or croak $!;
	my $pos = tell $fh;
	if ($pos < $offset) {
		for (1 .. (($offset - $pos) / 41)) {
			print $fh (('0' x 40),"\n") or croak $!;
		}
	}
	seek $fh, $offset, 0 or croak $!;
	print $fh $commit,"\n" or croak $!;
}

sub mkfile {
	my ($path) = @_;
	unless (-e $path) {
		my ($dir, $base) = ($path =~ m#^(.*?)/?([^/]+)$#);
		mkpath([$dir]) unless -d $dir;
		open my $fh, '>>', $path or die "Couldn't create $path: $!\n";
		close $fh or die "Couldn't close (create) $path: $!\n";
	}
}

sub rev_db_set {
	my ($self, $rev, $commit, $update_ref, $uuid) = @_;
	length $commit == 40 or die "arg3 must be a full SHA1 hexsum\n";
	my $db = $self->db_path($uuid);
	my $db_lock = "$db.lock";
	my $sig;
	if ($update_ref) {
		$SIG{INT} = $SIG{HUP} = $SIG{TERM} = $SIG{ALRM} = $SIG{PIPE} =
		            $SIG{USR1} = $SIG{USR2} = sub { $sig = $_[0] };
	}
	mkfile($db);

	$LOCKFILES{$db_lock} = 1;
	my $sync;
	# both of these options make our .rev_db file very, very important
	# and we can't afford to lose it because rebuild() won't work
	if ($self->use_svm_props || $self->no_metadata) {
		$sync = 1;
		copy($db, $db_lock) or die "rev_db_set(@_): ",
					   "Failed to copy: ",
					   "$db => $db_lock ($!)\n";
	} else {
		rename $db, $db_lock or die "rev_db_set(@_): ",
					    "Failed to rename: ",
					    "$db => $db_lock ($!)\n";
	}
	open my $fh, '+<', $db_lock or die "Couldn't open $db_lock: $!\n";
	_rev_db_set($fh, $rev, $commit);
	if ($sync) {
		$fh->flush or die "Couldn't flush $db_lock: $!\n";
		$fh->sync or die "Couldn't sync $db_lock: $!\n";
	}
	close $fh or croak $!;
	if ($update_ref) {
		$_head = $self;
		command_noisy('update-ref', '-m', "r$rev",
		              $self->refname, $commit);
	}
	rename $db_lock, $db or die "rev_db_set(@_): ", "Failed to rename: ",
	                            "$db_lock => $db ($!)\n";
	delete $LOCKFILES{$db_lock};
	if ($update_ref) {
		$SIG{INT} = $SIG{HUP} = $SIG{TERM} = $SIG{ALRM} = $SIG{PIPE} =
		            $SIG{USR1} = $SIG{USR2} = 'DEFAULT';
		kill $sig, $$ if defined $sig;
	}
}

sub rev_db_max {
	my ($self) = @_;
	$self->rebuild;
	my $db_path = $self->db_path;
	my @stat = stat $db_path or return 0;
	($stat[7] % 41) == 0 or die "$db_path inconsistent size: $stat[7]\n";
	my $max = $stat[7] / 41;
	(($max > 0) ? $max - 1 : 0);
}

sub rev_db_get {
	my ($self, $rev, $uuid) = @_;
	my $ret;
	my $offset = $rev * 41;
	my $db_path = $self->db_path($uuid);
	return undef unless -e $db_path;
	open my $fh, '<', $db_path or croak $!;
	if (sysseek($fh, $offset, 0) == $offset) {
		my $read = sysread($fh, $ret, 40);
		$ret = undef if ($read != 40 || $ret eq ('0'x40));
	}
	close $fh or croak $!;
	$ret;
}

sub find_rev_before {
	my ($self, $rev, $eq_ok) = @_;
	--$rev unless $eq_ok;
	while ($rev > 0) {
		if (my $c = $self->rev_db_get($rev)) {
			return ($rev, $c);
		}
		--$rev;
	}
	return (undef, undef);
}

sub _new {
	my ($class, $repo_id, $ref_id, $path) = @_;
	unless (defined $repo_id && length $repo_id) {
		$repo_id = $Git::SVN::default_repo_id;
	}
	unless (defined $ref_id && length $ref_id) {
		$_[2] = $ref_id = $Git::SVN::default_ref_id;
	}
	$_[1] = $repo_id = sanitize_remote_name($repo_id);
	my $dir = "$ENV{GIT_DIR}/svn/$ref_id";
	$_[3] = $path = '' unless (defined $path);
	mkpath(["$ENV{GIT_DIR}/svn"]);
	bless {
		ref_id => $ref_id, dir => $dir, index => "$dir/index",
	        path => $path, config => "$ENV{GIT_DIR}/svn/config",
	        db_root => "$dir/.rev_db", repo_id => $repo_id }, $class;
}

sub db_path {
	my ($self, $uuid) = @_;
	$uuid ||= $self->ra_uuid;
	"$self->{db_root}.$uuid";
}

sub uri_encode {
	my ($f) = @_;
	$f =~ s#([^a-zA-Z0-9\*!\:_\./\-])#uc sprintf("%%%02x",ord($1))#eg;
	$f
}

sub remove_username {
	$_[0] =~ s{^([^:]*://)[^@]+@}{$1};
}

package Git::SVN::Prompt;
use strict;
use warnings;
require SVN::Core;
use vars qw/$_no_auth_cache $_username/;

sub simple {
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
		username($cred, $realm, $may_save, $pool);
	}
	$cred->password(_read_password("Password for '" .
	                               $cred->username . "': ", $realm));
	$cred->may_save($may_save);
	$SVN::_Core::SVN_NO_ERROR;
}

sub ssl_server_trust {
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

sub ssl_client_cert {
	my ($cred, $realm, $may_save, $pool) = @_;
	$may_save = undef if $_no_auth_cache;
	print STDERR "Client certificate filename: ";
	STDERR->flush;
	chomp(my $filename = <STDIN>);
	$cred->cert_file($filename);
	$cred->may_save($may_save);
	$SVN::_Core::SVN_NO_ERROR;
}

sub ssl_client_cert_pw {
	my ($cred, $realm, $may_save, $pool) = @_;
	$may_save = undef if $_no_auth_cache;
	$cred->password(_read_password("Password: ", $realm));
	$cred->may_save($may_save);
	$SVN::_Core::SVN_NO_ERROR;
}

sub username {
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

package main;

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
use Digest::MD5;

# file baton members: path, mode_a, mode_b, pool, fh, blob, base
sub new {
	my ($class, $git_svn) = @_;
	my $self = SVN::Delta::Editor->new;
	bless $self, $class;
	$self->{c} = $git_svn->{last_commit} if exists $git_svn->{last_commit};
	$self->{empty} = {};
	$self->{dir_prop} = {};
	$self->{file_prop} = {};
	$self->{absent_dir} = {};
	$self->{absent_file} = {};
	$self->{gii} = $git_svn->tmp_index_do(sub { Git::IndexInfo->new });
	$self;
}

sub set_path_strip {
	my ($self, $path) = @_;
	$self->{path_strip} = qr/^\Q$path\E(\/|$)/ if length $path;
}

sub open_root {
	{ path => '' };
}

sub open_directory {
	my ($self, $path, $pb, $rev) = @_;
	{ path => $path };
}

sub git_path {
	my ($self, $path) = @_;
	if ($self->{path_strip}) {
		$path =~ s!$self->{path_strip}!! or
		  die "Failed to strip path '$path' ($self->{path_strip})\n";
	}
	$path;
}

sub delete_entry {
	my ($self, $path, $rev, $pb) = @_;

	my $gpath = $self->git_path($path);
	return undef if ($gpath eq '');

	# remove entire directories.
	if (command('ls-tree', $self->{c}, '--', $gpath) =~ /^040000 tree/) {
		my ($ls, $ctx) = command_output_pipe(qw/ls-tree
		                                     -r --name-only -z/,
				                     $self->{c}, '--', $gpath);
		local $/ = "\0";
		while (<$ls>) {
			chomp;
			$self->{gii}->remove($_);
			print "\tD\t$_\n" unless $::_q;
		}
		print "\tD\t$gpath/\n" unless $::_q;
		command_close_pipe($ls, $ctx);
		$self->{empty}->{$path} = 0
	} else {
		$self->{gii}->remove($gpath);
		print "\tD\t$gpath\n" unless $::_q;
	}
	undef;
}

sub open_file {
	my ($self, $path, $pb, $rev) = @_;
	my $gpath = $self->git_path($path);
	my ($mode, $blob) = (command('ls-tree', $self->{c}, '--', $gpath)
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
	my $path = $self->git_path($fb->{path});
	if (my $fh = $fb->{fh}) {
		if (defined $exp) {
			seek($fh, 0, 0) or croak $!;
			my $md5 = Digest::MD5->new;
			$md5->addfile($fh);
			my $got = $md5->hexdigest;
			if ($got ne $exp) {
				die "Checksum mismatch: $path\n",
				    "expected: $exp\n    got: $got\n";
			}
		}
		sysseek($fh, 0, 0) or croak $!;
		if ($fb->{mode_b} == 120000) {
			sysread($fh, my $buf, 5) == 5 or croak $!;
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
	$self->{gii}->update($fb->{mode_b}, $hash, $path) or croak $!;
	print "\t$fb->{action}\t$path\n" if $fb->{action} && ! $::_q;
	undef;
}

sub abort_edit {
	my $self = shift;
	$self->{nr} = $self->{gii}->{nr};
	delete $self->{gii};
	$self->SUPER::abort_edit(@_);
}

sub close_edit {
	my $self = shift;
	$self->{git_commit_ok} = 1;
	$self->{nr} = $self->{gii}->{nr};
	delete $self->{gii};
	$self->SUPER::close_edit(@_);
}

package SVN::Git::Editor;
use vars qw/@ISA $_rmdir $_cp_similarity $_find_copies_harder $_rename_limit/;
use strict;
use warnings;
use Carp qw/croak/;
use IO::File;
use Digest::MD5;

sub new {
	my ($class, $opts) = @_;
	foreach (qw/svn_path r ra tree_a tree_b log editor_cb/) {
		die "$_ required!\n" unless (defined $opts->{$_});
	}

	my $pool = SVN::Pool->new;
	my $mods = generate_diff($opts->{tree_a}, $opts->{tree_b});
	my $types = check_diff_paths($opts->{ra}, $opts->{svn_path},
	                             $opts->{r}, $mods);

	# $opts->{ra} functions should not be used after this:
	my @ce  = $opts->{ra}->get_commit_editor($opts->{log},
	                                        $opts->{editor_cb}, $pool);
	my $self = SVN::Delta::Editor->new(@ce, $pool);
	bless $self, $class;
	foreach (qw/svn_path r tree_a tree_b/) {
		$self->{$_} = $opts->{$_};
	}
	$self->{url} = $opts->{ra}->{url};
	$self->{mods} = $mods;
	$self->{types} = $types;
	$self->{pool} = $pool;
	$self->{bat} = { '' => $self->open_root($self->{r}, $self->{pool}) };
	$self->{rm} = { };
	$self->{path_prefix} = length $self->{svn_path} ?
	                       "$self->{svn_path}/" : '';
	return $self;
}

sub generate_diff {
	my ($tree_a, $tree_b) = @_;
	my @diff_tree = qw(diff-tree -z -r);
	if ($_cp_similarity) {
		push @diff_tree, "-C$_cp_similarity";
	} else {
		push @diff_tree, '-C';
	}
	push @diff_tree, '--find-copies-harder' if $_find_copies_harder;
	push @diff_tree, "-l$_rename_limit" if defined $_rename_limit;
	push @diff_tree, $tree_a, $tree_b;
	my ($diff_fh, $ctx) = command_output_pipe(@diff_tree);
	local $/ = "\0";
	my $state = 'meta';
	my @mods;
	while (<$diff_fh>) {
		chomp $_; # this gets rid of the trailing "\0"
		if ($state eq 'meta' && /^:(\d{6})\s(\d{6})\s
					$::sha1\s($::sha1)\s
					([MTCRAD])\d*$/xo) {
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
	\@mods;
}

sub check_diff_paths {
	my ($ra, $pfx, $rev, $mods) = @_;
	my %types;
	$pfx .= '/' if length $pfx;

	sub type_diff_paths {
		my ($ra, $types, $path, $rev) = @_;
		my @p = split m#/+#, $path;
		my $c = shift @p;
		unless (defined $types->{$c}) {
			$types->{$c} = $ra->check_path($c, $rev);
		}
		while (@p) {
			$c .= '/' . shift @p;
			next if defined $types->{$c};
			$types->{$c} = $ra->check_path($c, $rev);
		}
	}

	foreach my $m (@$mods) {
		foreach my $f (qw/file_a file_b/) {
			next unless defined $m->{$f};
			my ($dir) = ($m->{$f} =~ m#^(.*?)/?(?:[^/]+)$#);
			if (length $pfx.$dir && ! defined $types{$dir}) {
				type_diff_paths($ra, \%types, $pfx.$dir, $rev);
			}
		}
	}
	\%types;
}

sub split_path {
	return ($_[0] =~ m#^(.*?)/?([^/]+)$#);
}

sub repo_path {
	my ($self, $path) = @_;
	$self->{path_prefix}.(defined $path ? $path : '');
}

sub url_path {
	my ($self, $path) = @_;
	$self->{url} . '/' . $self->repo_path($path);
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

	my ($fh, $ctx) = command_output_pipe(qw/ls-tree --name-only -r -z/,
	                                     $self->{tree_b});
	local $/ = "\0";
	while (<$fh>) {
		chomp;
		my @dn = split m#/#, $_;
		while (pop @dn) {
			delete $rm->{join '/', @dn};
		}
		unless (%$rm) {
			close $fh;
			return;
		}
	}
	command_close_pipe($fh, $ctx);

	my ($r, $p, $bat) = ($self->{r}, $self->{pool}, $self->{bat});
	foreach my $d (sort { $b =~ tr#/#/# <=> $a =~ tr#/#/# } keys %$rm) {
		$self->close_directory($bat->{$d}, $p);
		my ($dn) = ($d =~ m#^(.*?)/?(?:[^/]+)$#);
		print "\tD+\t$d/\n" unless $::_q;
		$self->SUPER::delete_entry($d, $r, $bat->{$dn}, $p);
		delete $bat->{$d};
	}
}

sub open_or_add_dir {
	my ($self, $full_path, $baton) = @_;
	my $t = $self->{types}->{$full_path};
	if (!defined $t) {
		die "$full_path not known in r$self->{r} or we have a bug!\n";
	}
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
	my $repo_path = $self->repo_path($path);
	return $bat->{''} unless (length $repo_path);
	my @p = split m#/+#, $repo_path;
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
	print "\tA\t$m->{file_b}\n" unless $::_q;
	$self->chg_file($fbat, $m);
	$self->close_file($fbat,undef,$self->{pool});
}

sub C {
	my ($self, $m) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
				$self->url_path($m->{file_a}), $self->{r});
	print "\tC\t$m->{file_a} => $m->{file_b}\n" unless $::_q;
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
	print "\tR\t$m->{file_a} => $m->{file_b}\n" unless $::_q;
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
	print "\t$m->{chg}\t$m->{file_b}\n" unless $::_q;
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
	my ($self, $m) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	print "\tD\t$m->{file_b}\n" unless $::_q;
	$self->delete_entry($m->{file_b}, $pbat);
}

sub close_edit {
	my ($self) = @_;
	my ($p,$bat) = ($self->{pool}, $self->{bat});
	foreach (sort { $b =~ tr#/#/# <=> $a =~ tr#/#/# } keys %$bat) {
		next if $_ eq '';
		$self->close_directory($bat->{$_}, $p);
	}
	$self->close_directory($bat->{''}, $p);
	$self->SUPER::close_edit($p);
	$p->clear;
}

sub abort_edit {
	my ($self) = @_;
	$self->SUPER::abort_edit($self->{pool});
}

sub DESTROY {
	my $self = shift;
	$self->SUPER::DESTROY(@_);
	$self->{pool}->clear;
}

# this drives the editor
sub apply_diff {
	my ($self) = @_;
	my $mods = $self->{mods};
	my %o = ( D => 1, R => 0, C => -1, A => 3, M => 3, T => 3 );
	foreach my $m (sort { $o{$a->{chg}} <=> $o{$b->{chg}} } @$mods) {
		my $f = $m->{chg};
		if (defined $o{$f}) {
			$self->$f($m);
		} else {
			fatal("Invalid change type: $f\n");
		}
	}
	$self->rmdirs if $_rmdir;
	if (@$mods == 0) {
		$self->abort_edit;
	} else {
		$self->close_edit;
	}
	return scalar @$mods;
}

package Git::SVN::Ra;
use vars qw/@ISA $config_dir $_log_window_size/;
use strict;
use warnings;
my ($can_do_switch, %ignored_err, $RA);

BEGIN {
	# enforce temporary pool usage for some simple functions
	no strict 'refs';
	for my $f (qw/rev_proplist get_latest_revnum get_uuid get_repos_root/) {
		my $SUPER = "SUPER::$f";
		*$f = sub {
			my $self = shift;
			my $pool = SVN::Pool->new;
			my @ret = $self->$SUPER(@_,$pool);
			$pool->clear;
			wantarray ? @ret : $ret[0];
		};
	}
}

sub new {
	my ($class, $url) = @_;
	$url =~ s!/+$!!;
	return $RA if ($RA && $RA->{url} eq $url);

	SVN::_Core::svn_config_ensure($config_dir, undef);
	my ($baton, $callbacks) = SVN::Core::auth_open_helper([
	    SVN::Client::get_simple_provider(),
	    SVN::Client::get_ssl_server_trust_file_provider(),
	    SVN::Client::get_simple_prompt_provider(
	      \&Git::SVN::Prompt::simple, 2),
	    SVN::Client::get_ssl_client_cert_file_provider(),
	    SVN::Client::get_ssl_client_cert_prompt_provider(
	      \&Git::SVN::Prompt::ssl_client_cert, 2),
	    SVN::Client::get_ssl_client_cert_pw_prompt_provider(
	      \&Git::SVN::Prompt::ssl_client_cert_pw, 2),
	    SVN::Client::get_username_provider(),
	    SVN::Client::get_ssl_server_trust_prompt_provider(
	      \&Git::SVN::Prompt::ssl_server_trust),
	    SVN::Client::get_username_prompt_provider(
	      \&Git::SVN::Prompt::username, 2),
	  ]);
	my $config = SVN::Core::config_get_config($config_dir);
	my $self = SVN::Ra->new(url => $url, auth => $baton,
	                      config => $config,
			      pool => SVN::Pool->new,
	                      auth_provider_callbacks => $callbacks);
	$self->{svn_path} = $url;
	$self->{repos_root} = $self->get_repos_root;
	$self->{svn_path} =~ s#^\Q$self->{repos_root}\E(/|$)##;
	$self->{cache} = { check_path => { r => 0, data => {} },
	                   get_dir => { r => 0, data => {} } };
	$RA = bless $self, $class;
}

sub check_path {
	my ($self, $path, $r) = @_;
	my $cache = $self->{cache}->{check_path};
	if ($r == $cache->{r} && exists $cache->{data}->{$path}) {
		return $cache->{data}->{$path};
	}
	my $pool = SVN::Pool->new;
	my $t = $self->SUPER::check_path($path, $r, $pool);
	$pool->clear;
	if ($r != $cache->{r}) {
		%{$cache->{data}} = ();
		$cache->{r} = $r;
	}
	$cache->{data}->{$path} = $t;
}

sub get_dir {
	my ($self, $dir, $r) = @_;
	my $cache = $self->{cache}->{get_dir};
	if ($r == $cache->{r}) {
		if (my $x = $cache->{data}->{$dir}) {
			return wantarray ? @$x : $x->[0];
		}
	}
	my $pool = SVN::Pool->new;
	my ($d, undef, $props) = $self->SUPER::get_dir($dir, $r, $pool);
	my %dirents = map { $_ => { kind => $d->{$_}->kind } } keys %$d;
	$pool->clear;
	if ($r != $cache->{r}) {
		%{$cache->{data}} = ();
		$cache->{r} = $r;
	}
	$cache->{data}->{$dir} = [ \%dirents, $r, $props ];
	wantarray ? (\%dirents, $r, $props) : \%dirents;
}

sub DESTROY {
	# do not call the real DESTROY since we store ourselves in $RA
}

sub get_log {
	my ($self, @args) = @_;
	my $pool = SVN::Pool->new;
	splice(@args, 3, 1) if ($SVN::Core::VERSION le '1.2.0');
	my $ret = $self->SUPER::get_log(@args, $pool);
	$pool->clear;
	$ret;
}

sub get_commit_editor {
	my ($self, $log, $cb, $pool) = @_;
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef, 0) : ();
	$self->SUPER::get_commit_editor($log, $cb, @lock, $pool);
}

sub gs_do_update {
	my ($self, $rev_a, $rev_b, $gs, $editor) = @_;
	my $new = ($rev_a == $rev_b);
	my $path = $gs->{path};

	if ($new && -e $gs->{index}) {
		unlink $gs->{index} or die
		  "Couldn't unlink index: $gs->{index}: $!\n";
	}
	my $pool = SVN::Pool->new;
	$editor->set_path_strip($path);
	my (@pc) = split m#/#, $path;
	my $reporter = $self->do_update($rev_b, (@pc ? shift @pc : ''),
	                                1, $editor, $pool);
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef) : ();

	# Since we can't rely on svn_ra_reparent being available, we'll
	# just have to do some magic with set_path to make it so
	# we only want a partial path.
	my $sp = '';
	my $final = join('/', @pc);
	while (@pc) {
		$reporter->set_path($sp, $rev_b, 0, @lock, $pool);
		$sp .= '/' if length $sp;
		$sp .= shift @pc;
	}
	die "BUG: '$sp' != '$final'\n" if ($sp ne $final);

	$reporter->set_path($sp, $rev_a, $new, @lock, $pool);

	$reporter->finish_report($pool);
	$pool->clear;
	$editor->{git_commit_ok};
}

# this requires SVN 1.4.3 or later (do_switch didn't work before 1.4.3, and
# svn_ra_reparent didn't work before 1.4)
sub gs_do_switch {
	my ($self, $rev_a, $rev_b, $gs, $url_b, $editor) = @_;
	my $path = $gs->{path};
	my $pool = SVN::Pool->new;

	my $full_url = $self->{url};
	my $old_url = $full_url;
	$full_url .= "/$path" if length $path;
	my ($ra, $reparented);
	if ($old_url ne $full_url) {
		if ($old_url !~ m#^svn(\+ssh)?://#) {
			SVN::_Ra::svn_ra_reparent($self->{session}, $full_url,
			                          $pool);
			$self->{url} = $full_url;
			$reparented = 1;
		} else {
			$ra = Git::SVN::Ra->new($full_url);
		}
	}
	$ra ||= $self;
	my $reporter = $ra->do_switch($rev_b, '', 1, $url_b, $editor, $pool);
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef) : ();
	$reporter->set_path('', $rev_a, 0, @lock, $pool);
	$reporter->finish_report($pool);

	if ($reparented) {
		SVN::_Ra::svn_ra_reparent($self->{session}, $old_url, $pool);
		$self->{url} = $old_url;
	}

	$pool->clear;
	$editor->{git_commit_ok};
}

sub longest_common_path {
	my ($gsv, $globs) = @_;
	my %common;
	my $common_max = scalar @$gsv;

	foreach my $gs (@$gsv) {
		my @tmp = split m#/#, $gs->{path};
		my $p = '';
		foreach (@tmp) {
			$p .= length($p) ? "/$_" : $_;
			$common{$p} ||= 0;
			$common{$p}++;
		}
	}
	$globs ||= [];
	$common_max += scalar @$globs;
	foreach my $glob (@$globs) {
		my @tmp = split m#/#, $glob->{path}->{left};
		my $p = '';
		foreach (@tmp) {
			$p .= length($p) ? "/$_" : $_;
			$common{$p} ||= 0;
			$common{$p}++;
		}
	}

	my $longest_path = '';
	foreach (sort {length $b <=> length $a} keys %common) {
		if ($common{$_} == $common_max) {
			$longest_path = $_;
			last;
		}
	}
	$longest_path;
}

sub gs_fetch_loop_common {
	my ($self, $base, $head, $gsv, $globs) = @_;
	return if ($base > $head);
	my $inc = $_log_window_size;
	my ($min, $max) = ($base, $head < $base + $inc ? $head : $base + $inc);
	my $longest_path = longest_common_path($gsv, $globs);
	while (1) {
		my %revs;
		my $err;
		my $err_handler = $SVN::Error::handler;
		$SVN::Error::handler = sub {
			($err) = @_;
			skip_unknown_revs($err);
		};
		sub _cb {
			my ($paths, $r, $author, $date, $log) = @_;
			[ dup_changed_paths($paths),
			  { author => $author, date => $date, log => $log } ];
		}
		$self->get_log([$longest_path], $min, $max, 0, 1, 1,
		               sub { $revs{$_[1]} = _cb(@_) });
		if ($err && $max >= $head) {
			print STDERR "Path '$longest_path' ",
				     "was probably deleted:\n",
				     $err->expanded_message,
				     "\nWill attempt to follow ",
				     "revisions r$min .. r$max ",
				     "committed before the deletion\n";
			my $hi = $max;
			while (--$hi >= $min) {
				my $ok;
				$self->get_log([$longest_path], $min, $hi,
				               0, 1, 1, sub {
				               $ok ||= $_[1];
				               $revs{$_[1]} = _cb(@_) });
				if ($ok) {
					print STDERR "r$min .. r$ok OK\n";
					last;
				}
			}
		}
		$SVN::Error::handler = $err_handler;

		my %exists = map { $_->{path} => $_ } @$gsv;
		foreach my $r (sort {$a <=> $b} keys %revs) {
			my ($paths, $logged) = @{$revs{$r}};

			foreach my $gs ($self->match_globs(\%exists, $paths,
			                                   $globs, $r)) {
				if ($gs->rev_db_max >= $r) {
					next;
				}
				next unless $gs->match_paths($paths, $r);
				$gs->{logged_rev_props} = $logged;
				if (my $last_commit = $gs->last_commit) {
					$gs->assert_index_clean($last_commit);
				}
				my $log_entry = $gs->do_fetch($paths, $r);
				if ($log_entry) {
					$gs->do_git_commit($log_entry);
				}
			}
			foreach my $g (@$globs) {
				my $k = "svn-remote.$g->{remote}." .
				        "$g->{t}-maxRev";
				Git::SVN::tmp_config($k, $r);
			}
		}
		# pre-fill the .rev_db since it'll eventually get filled in
		# with '0' x40 if something new gets committed
		foreach my $gs (@$gsv) {
			next if defined $gs->rev_db_get($max);
			$gs->rev_db_set($max, 0 x40);
		}
		foreach my $g (@$globs) {
			my $k = "svn-remote.$g->{remote}.$g->{t}-maxRev";
			Git::SVN::tmp_config($k, $max);
		}
		last if $max >= $head;
		$min = $max + 1;
		$max += $inc;
		$max = $head if ($max > $head);
	}
}

sub match_globs {
	my ($self, $exists, $paths, $globs, $r) = @_;

	sub get_dir_check {
		my ($self, $exists, $g, $r) = @_;
		my @x = eval { $self->get_dir($g->{path}->{left}, $r) };
		return unless scalar @x == 3;
		my $dirents = $x[0];
		foreach my $de (keys %$dirents) {
			next if $dirents->{$de}->{kind} != $SVN::Node::dir;
			my $p = $g->{path}->full_path($de);
			next if $exists->{$p};
			next if (length $g->{path}->{right} &&
				 ($self->check_path($p, $r) !=
				  $SVN::Node::dir));
			$exists->{$p} = Git::SVN->init($self->{url}, $p, undef,
					 $g->{ref}->full_path($de), 1);
		}
	}
	foreach my $g (@$globs) {
		if (my $path = $paths->{"/$g->{path}->{left}"}) {
			if ($path->{action} =~ /^[AR]$/) {
				get_dir_check($self, $exists, $g, $r);
			}
		}
		foreach (keys %$paths) {
			if (/$g->{path}->{left_regex}/ &&
			    !/$g->{path}->{regex}/) {
				next if $paths->{$_}->{action} !~ /^[AR]$/;
				get_dir_check($self, $exists, $g, $r);
			}
			next unless /$g->{path}->{regex}/;
			my $p = $1;
			my $pathname = $g->{path}->full_path($p);
			next if $exists->{$pathname};
			next if ($self->check_path($pathname, $r) !=
			         $SVN::Node::dir);
			$exists->{$pathname} = Git::SVN->init(
			                      $self->{url}, $pathname, undef,
			                      $g->{ref}->full_path($p), 1);
		}
		my $c = '';
		foreach (split m#/#, $g->{path}->{left}) {
			$c .= "/$_";
			next unless ($paths->{$c} &&
			             ($paths->{$c}->{action} =~ /^[AR]$/));
			get_dir_check($self, $exists, $g, $r);
		}
	}
	values %$exists;
}

sub minimize_url {
	my ($self) = @_;
	return $self->{url} if ($self->{url} eq $self->{repos_root});
	my $url = $self->{repos_root};
	my @components = split(m!/!, $self->{svn_path});
	my $c = '';
	do {
		$url .= "/$c" if length $c;
		eval { (ref $self)->new($url)->get_latest_revnum };
	} while ($@ && ($c = shift @components));
	$url;
}

sub can_do_switch {
	my $self = shift;
	unless (defined $can_do_switch) {
		my $pool = SVN::Pool->new;
		my $rep = eval {
			$self->do_switch(1, '', 0, $self->{url},
			                 SVN::Delta::Editor->new, $pool);
		};
		if ($@) {
			$can_do_switch = 0;
		} else {
			$rep->abort_report($pool);
			$can_do_switch = 1;
		}
		$pool->clear;
	}
	$can_do_switch;
}

sub skip_unknown_revs {
	my ($err) = @_;
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
		my $err_key = $err->expanded_message;
		# revision numbers change every time, filter them out
		$err_key =~ s/\d+/\0/g;
		$err_key = "$errno\0$err_key";
		unless ($ignored_err{$err_key}) {
			warn "W: Ignoring error from SVN, path probably ",
			     "does not exist: ($errno): ",
			     $err->expanded_message,"\n";
			$ignored_err{$err_key} = 1;
		}
		return;
	}
	die "Error from SVN, ($errno): ", $err->expanded_message,"\n";
}

# svn_log_changed_path_t objects passed to get_log are likely to be
# overwritten even if only the refs are copied to an external variable,
# so we should dup the structures in their entirety.  Using an externally
# passed pool (instead of our temporary and quickly cleared pool in
# Git::SVN::Ra) does not help matters at all...
sub dup_changed_paths {
	my ($paths) = @_;
	return undef unless $paths;
	my %ret;
	foreach my $p (keys %$paths) {
		my $i = $paths->{$p};
		my %s = map { $_ => $i->$_ }
		              qw/copyfrom_path copyfrom_rev action/;
		$ret{$p} = \%s;
	}
	\%ret;
}

package Git::SVN::Log;
use strict;
use warnings;
use POSIX qw/strftime/;
use vars qw/$TZ $limit $color $pager $non_recursive $verbose $oneline
            %rusers $show_commit $incremental/;
my $l_fmt;

sub cmt_showable {
	my ($c) = @_;
	return 1 if defined $c->{r};

	# big commit message got truncated by the 16k pretty buffer in rev-list
	if ($c->{l} && $c->{l}->[-1] eq "...\n" &&
				$c->{a_raw} =~ /\@([a-f\d\-]+)>$/) {
		@{$c->{l}} = ();
		my @log = command(qw/cat-file commit/, $c->{c});

		# shift off the headers
		shift @log while ($log[0] ne '');
		shift @log;

		# TODO: make $c->{l} not have a trailing newline in the future
		@{$c->{l}} = map { "$_\n" } grep !/^git-svn-id: /, @log;

		(undef, $c->{r}, undef) = ::extract_metadata(
				(grep(/^git-svn-id: /, @log))[-1]);
	}
	return defined $c->{r};
}

sub log_use_color {
	return 1 if $color;
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
		if (-t *STDOUT || (defined $pager && $pc eq 'true')) {
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
	my ($r_min, $r_max, @args) = @_;
	my $head = 'HEAD';
	foreach my $x (@args) {
		last if $x eq '--';
		next unless ::verify_ref("$x^0");
		$head = $x;
		last;
	}

	my ($url, $rev, $uuid, $gs) = ::working_head_info($head);
	$gs ||= Git::SVN->_new;
	my @cmd = (qw/log --abbrev-commit --pretty=raw --default/,
	           $gs->refname);
	push @cmd, '-r' unless $non_recursive;
	push @cmd, qw/--raw --name-status/ if $verbose;
	push @cmd, '--color' if log_use_color();
	return @cmd unless defined $r_max;
	if ($r_max == $r_min) {
		push @cmd, '--max-count=1';
		if (my $c = $gs->rev_db_get($r_max)) {
			push @cmd, $c;
		}
	} else {
		my ($c_min, $c_max);
		$c_max = $gs->rev_db_get($r_max);
		$c_min = $gs->rev_db_get($r_min);
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

# adapted from pager.c
sub config_pager {
	$pager ||= $ENV{GIT_PAGER} || $ENV{PAGER};
	if (!defined $pager) {
		$pager = 'less';
	} elsif (length $pager == 0 || $pager eq 'cat') {
		$pager = undef;
	}
}

sub run_pager {
	return unless -t *STDOUT;
	pipe my $rfd, my $wfd or return;
	defined(my $pid = fork) or ::fatal "Can't fork: $!\n";
	if (!$pid) {
		open STDOUT, '>&', $wfd or
		                     ::fatal "Can't redirect to stdout: $!\n";
		return;
	}
	open STDIN, '<&', $rfd or ::fatal "Can't redirect stdin: $!\n";
	$ENV{LESS} ||= 'FRSX';
	exec $pager or ::fatal "Can't run pager: $! ($pager)\n";
}

sub tz_to_s_offset {
	my ($tz) = @_;
	$tz =~ s/(\d\d)$//;
	return ($1 * 60) + ($tz * 3600);
}

sub get_author_info {
	my ($dest, $author, $t, $tz) = @_;
	$author =~ s/(?:^\s*|\s*$)//g;
	$dest->{a_raw} = $author;
	my $au;
	if ($::_authors) {
		$au = $rusers{$author} || undef;
	}
	if (!$au) {
		($au) = ($author =~ /<([^>]+)\@[^>]+>$/);
	}
	$dest->{t} = $t;
	$dest->{tz} = $tz;
	$dest->{a} = $au;
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
			return 0 if (defined $limit && --$limit < 0);
			push @$defer, $c;
			return 1;
		}
		if ($r_min != $r_max) {
			return 1 if ($r_min < $c->{r});
			return 1 if ($r_max > $c->{r});
		}
	}
	return 0 if (defined $limit && --$limit < 0);
	show_commit($c);
	return 1;
}

sub show_commit {
	my $c = shift;
	if ($oneline) {
		my $x = "\n";
		if (my $l = $c->{l}) {
			while ($l->[0] =~ /^\s*$/) { shift @$l }
			$x = $l->[0];
		}
		$l_fmt ||= 'A' . length($c->{r});
		print 'r',pack($l_fmt, $c->{r}),' | ';
		print "$c->{c} | " if $show_commit;
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
	print "$c->{c} | " if $show_commit;
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
	foreach my $x (qw/raw stat diff/) {
		if ($c->{$x}) {
			print "\n";
			print $_ foreach @{$c->{$x}}
		}
	}
}

sub cmd_show_log {
	my (@args) = @_;
	my ($r_min, $r_max);
	my $r_last = -1; # prevent dupes
	if (defined $TZ) {
		$ENV{TZ} = $TZ;
	} else {
		delete $ENV{TZ};
	}
	if (defined $::_revision) {
		if ($::_revision =~ /^(\d+):(\d+)$/) {
			($r_min, $r_max) = ($1, $2);
		} elsif ($::_revision =~ /^\d+$/) {
			$r_min = $r_max = $::_revision;
		} else {
			::fatal "-r$::_revision is not supported, use ",
				"standard \'git log\' arguments instead\n";
		}
	}

	config_pager();
	@args = (git_svn_log_cmd($r_min, $r_max, @args), @args);
	my $log = command_output_pipe(@args);
	run_pager();
	my (@k, $c, $d, $stat);
	my $esc_color = qr/(?:\033\[(?:(?:\d+;)*\d*)?m)*/;
	while (<$log>) {
		if (/^${esc_color}commit ($::sha1_short)/o) {
			my $cmt = $1;
			if ($c && cmt_showable($c) && $c->{r} != $r_last) {
				$r_last = $c->{r};
				process_commit($c, $r_min, $r_max, \@k) or
								goto out;
			}
			$d = undef;
			$c = { c => $cmt };
		} elsif (/^${esc_color}author (.+) (\d+) ([\-\+]?\d+)$/o) {
			get_author_info($c, $1, $2, $3);
		} elsif (/^${esc_color}(?:tree|parent|committer) /o) {
			# ignore
		} elsif (/^${esc_color}:\d{6} \d{6} $::sha1_short/o) {
			push @{$c->{raw}}, $_;
		} elsif (/^${esc_color}[ACRMDT]\t/) {
			# we could add $SVN->{svn_path} here, but that requires
			# remote access at the moment (repo_path_split)...
			s#^(${esc_color})([ACRMDT])\t#$1   $2 #o;
			push @{$c->{changed}}, $_;
		} elsif (/^${esc_color}diff /o) {
			$d = 1;
			push @{$c->{diff}}, $_;
		} elsif ($d) {
			push @{$c->{diff}}, $_;
		} elsif (/^\ .+\ \|\s*\d+\ $esc_color[\+\-]*
		          $esc_color*[\+\-]*$esc_color$/x) {
			$stat = 1;
			push @{$c->{stat}}, $_;
		} elsif ($stat && /^ \d+ files changed, \d+ insertions/) {
			push @{$c->{stat}}, $_;
			$stat = undef;
		} elsif (/^${esc_color}    (git-svn-id:.+)$/o) {
			($c->{url}, $c->{r}, undef) = ::extract_metadata($1);
		} elsif (s/^${esc_color}    //o) {
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
	print '-' x72,"\n" unless $incremental || $oneline;
}

package Git::SVN::Migration;
# these version numbers do NOT correspond to actual version numbers
# of git nor git-svn.  They are just relative.
#
# v0 layout: .git/$id/info/url, refs/heads/$id-HEAD
#
# v1 layout: .git/$id/info/url, refs/remotes/$id
#
# v2 layout: .git/svn/$id/info/url, refs/remotes/$id
#
# v3 layout: .git/svn/$id, refs/remotes/$id
#            - info/url may remain for backwards compatibility
#            - this is what we migrate up to this layout automatically,
#            - this will be used by git svn init on single branches
# v3.1 layout (auto migrated):
#            - .rev_db => .rev_db.$UUID, .rev_db will remain as a symlink
#              for backwards compatibility
#
# v4 layout: .git/svn/$repo_id/$id, refs/remotes/$repo_id/$id
#            - this is only created for newly multi-init-ed
#              repositories.  Similar in spirit to the
#              --use-separate-remotes option in git-clone (now default)
#            - we do not automatically migrate to this (following
#              the example set by core git)
use strict;
use warnings;
use Carp qw/croak/;
use File::Path qw/mkpath/;
use File::Basename qw/dirname basename/;
use vars qw/$_minimize/;

sub migrate_from_v0 {
	my $git_dir = $ENV{GIT_DIR};
	return undef unless -d $git_dir;
	my ($fh, $ctx) = command_output_pipe(qw/rev-parse --symbolic --all/);
	my $migrated = 0;
	while (<$fh>) {
		chomp;
		my ($id, $orig_ref) = ($_, $_);
		next unless $id =~ s#^refs/heads/(.+)-HEAD$#$1#;
		next unless -f "$git_dir/$id/info/url";
		my $new_ref = "refs/remotes/$id";
		if (::verify_ref("$new_ref^0")) {
			print STDERR "W: $orig_ref is probably an old ",
			             "branch used by an ancient version of ",
				     "git-svn.\n",
				     "However, $new_ref also exists.\n",
				     "We will not be able ",
				     "to use this branch until this ",
				     "ambiguity is resolved.\n";
			next;
		}
		print STDERR "Migrating from v0 layout...\n" if !$migrated;
		print STDERR "Renaming ref: $orig_ref => $new_ref\n";
		command_noisy('update-ref', $new_ref, $orig_ref);
		command_noisy('update-ref', '-d', $orig_ref, $orig_ref);
		$migrated++;
	}
	command_close_pipe($fh, $ctx);
	print STDERR "Done migrating from v0 layout...\n" if $migrated;
	$migrated;
}

sub migrate_from_v1 {
	my $git_dir = $ENV{GIT_DIR};
	my $migrated = 0;
	return $migrated unless -d $git_dir;
	my $svn_dir = "$git_dir/svn";

	# just in case somebody used 'svn' as their $id at some point...
	return $migrated if -d $svn_dir && ! -f "$svn_dir/info/url";

	print STDERR "Migrating from a git-svn v1 layout...\n";
	mkpath([$svn_dir]);
	print STDERR "Data from a previous version of git-svn exists, but\n\t",
	             "$svn_dir\n\t(required for this version ",
	             "($::VERSION) of git-svn) does not. exist\n";
	my ($fh, $ctx) = command_output_pipe(qw/rev-parse --symbolic --all/);
	while (<$fh>) {
		my $x = $_;
		next unless $x =~ s#^refs/remotes/##;
		chomp $x;
		next unless -f "$git_dir/$x/info/url";
		my $u = eval { ::file_to_s("$git_dir/$x/info/url") };
		next unless $u;
		my $dn = dirname("$git_dir/svn/$x");
		mkpath([$dn]) unless -d $dn;
		if ($x eq 'svn') { # they used 'svn' as GIT_SVN_ID:
			mkpath(["$git_dir/svn/svn"]);
			print STDERR " - $git_dir/$x/info => ",
			                "$git_dir/svn/$x/info\n";
			rename "$git_dir/$x/info", "$git_dir/svn/$x/info" or
			       croak "$!: $x";
			# don't worry too much about these, they probably
			# don't exist with repos this old (save for index,
			# and we can easily regenerate that)
			foreach my $f (qw/unhandled.log index .rev_db/) {
				rename "$git_dir/$x/$f", "$git_dir/svn/$x/$f";
			}
		} else {
			print STDERR " - $git_dir/$x => $git_dir/svn/$x\n";
			rename "$git_dir/$x", "$git_dir/svn/$x" or
			       croak "$!: $x";
		}
		$migrated++;
	}
	command_close_pipe($fh, $ctx);
	print STDERR "Done migrating from a git-svn v1 layout\n";
	$migrated;
}

sub read_old_urls {
	my ($l_map, $pfx, $path) = @_;
	my @dir;
	foreach (<$path/*>) {
		if (-r "$_/info/url") {
			$pfx .= '/' if $pfx && $pfx !~ m!/$!;
			my $ref_id = $pfx . basename $_;
			my $url = ::file_to_s("$_/info/url");
			$l_map->{$ref_id} = $url;
		} elsif (-d $_) {
			push @dir, $_;
		}
	}
	foreach (@dir) {
		my $x = $_;
		$x =~ s!^\Q$ENV{GIT_DIR}\E/svn/!!o;
		read_old_urls($l_map, $x, $_);
	}
}

sub migrate_from_v2 {
	my @cfg = command(qw/config -l/);
	return if grep /^svn-remote\..+\.url=/, @cfg;
	my %l_map;
	read_old_urls(\%l_map, '', "$ENV{GIT_DIR}/svn");
	my $migrated = 0;

	foreach my $ref_id (sort keys %l_map) {
		eval { Git::SVN->init($l_map{$ref_id}, '', undef, $ref_id) };
		if ($@) {
			Git::SVN->init($l_map{$ref_id}, '', $ref_id, $ref_id);
		}
		$migrated++;
	}
	$migrated;
}

sub minimize_connections {
	my $r = Git::SVN::read_all_remotes();
	my $new_urls = {};
	my $root_repos = {};
	foreach my $repo_id (keys %$r) {
		my $url = $r->{$repo_id}->{url} or next;
		my $fetch = $r->{$repo_id}->{fetch} or next;
		my $ra = Git::SVN::Ra->new($url);

		# skip existing cases where we already connect to the root
		if (($ra->{url} eq $ra->{repos_root}) ||
		    (Git::SVN::sanitize_remote_name($ra->{repos_root}) eq
		     $repo_id)) {
			$root_repos->{$ra->{url}} = $repo_id;
			next;
		}

		my $root_ra = Git::SVN::Ra->new($ra->{repos_root});
		my $root_path = $ra->{url};
		$root_path =~ s#^\Q$ra->{repos_root}\E(/|$)##;
		foreach my $path (keys %$fetch) {
			my $ref_id = $fetch->{$path};
			my $gs = Git::SVN->new($ref_id, $repo_id, $path);

			# make sure we can read when connecting to
			# a higher level of a repository
			my ($last_rev, undef) = $gs->last_rev_commit;
			if (!defined $last_rev) {
				$last_rev = eval {
					$root_ra->get_latest_revnum;
				};
				next if $@;
			}
			my $new = $root_path;
			$new .= length $path ? "/$path" : '';
			eval {
				$root_ra->get_log([$new], $last_rev, $last_rev,
			                          0, 0, 1, sub { });
			};
			next if $@;
			$new_urls->{$ra->{repos_root}}->{$new} =
			        { ref_id => $ref_id,
				  old_repo_id => $repo_id,
				  old_path => $path };
		}
	}

	my @emptied;
	foreach my $url (keys %$new_urls) {
		# see if we can re-use an existing [svn-remote "repo_id"]
		# instead of creating a(n ugly) new section:
		my $repo_id = $root_repos->{$url} ||
		              Git::SVN::sanitize_remote_name($url);

		my $fetch = $new_urls->{$url};
		foreach my $path (keys %$fetch) {
			my $x = $fetch->{$path};
			Git::SVN->init($url, $path, $repo_id, $x->{ref_id});
			my $pfx = "svn-remote.$x->{old_repo_id}";

			my $old_fetch = quotemeta("$x->{old_path}:".
			                          "refs/remotes/$x->{ref_id}");
			command_noisy(qw/config --unset/,
			              "$pfx.fetch", '^'. $old_fetch . '$');
			delete $r->{$x->{old_repo_id}}->
			       {fetch}->{$x->{old_path}};
			if (!keys %{$r->{$x->{old_repo_id}}->{fetch}}) {
				command_noisy(qw/config --unset/,
				              "$pfx.url");
				push @emptied, $x->{old_repo_id}
			}
		}
	}
	if (@emptied) {
		my $file = $ENV{GIT_CONFIG} || $ENV{GIT_CONFIG_LOCAL} ||
		           "$ENV{GIT_DIR}/config";
		print STDERR <<EOF;
The following [svn-remote] sections in your config file ($file) are empty
and can be safely removed:
EOF
		print STDERR "[svn-remote \"$_\"]\n" foreach @emptied;
	}
}

sub migration_check {
	migrate_from_v0();
	migrate_from_v1();
	migrate_from_v2();
	minimize_connections() if $_minimize;
}

package Git::IndexInfo;
use strict;
use warnings;
use Git qw/command_input_pipe command_close_pipe/;

sub new {
	my ($class) = @_;
	my ($gui, $ctx) = command_input_pipe(qw/update-index -z --index-info/);
	bless { gui => $gui, ctx => $ctx, nr => 0}, $class;
}

sub remove {
	my ($self, $path) = @_;
	if (print { $self->{gui} } '0 ', 0 x 40, "\t", $path, "\0") {
		return ++$self->{nr};
	}
	undef;
}

sub update {
	my ($self, $mode, $hash, $path) = @_;
	if (print { $self->{gui} } $mode, ' ', $hash, "\t", $path, "\0") {
		return ++$self->{nr};
	}
	undef;
}

sub DESTROY {
	my ($self) = @_;
	command_close_pipe($self->{gui}, $self->{ctx});
}

package Git::SVN::GlobSpec;
use strict;
use warnings;

sub new {
	my ($class, $glob) = @_;
	my $re = $glob;
	$re =~ s!/+$!!g; # no need for trailing slashes
	my $nr = ($re =~ s!^(.*)\*(.*)$!\(\[^/\]+\)!g);
	my ($left, $right) = ($1, $2);
	if ($nr > 1) {
		die "Only one '*' wildcard expansion ",
		    "is supported (got $nr): '$glob'\n";
	} elsif ($nr == 0) {
		die "One '*' is needed for glob: '$glob'\n";
	}
	$re = quotemeta($left) . $re . quotemeta($right);
	if (length $left && !($left =~ s!/+$!!g)) {
		die "Missing trailing '/' on left side of: '$glob' ($left)\n";
	}
	if (length $right && !($right =~ s!^/+!!g)) {
		die "Missing leading '/' on right side of: '$glob' ($right)\n";
	}
	my $left_re = qr/^\/\Q$left\E(\/|$)/;
	bless { left => $left, right => $right, left_regex => $left_re,
	        regex => qr/$re/, glob => $glob }, $class;
}

sub full_path {
	my ($self, $path) = @_;
	return (length $self->{left} ? "$self->{left}/" : '') .
	       $path . (length $self->{right} ? "/$self->{right}" : '');
}

__END__

Data structures:


$remotes = { # returned by read_all_remotes()
	'svn' => {
		# svn-remote.svn.url=https://svn.musicpd.org
		url => 'https://svn.musicpd.org',
		# svn-remote.svn.fetch=mpd/trunk:trunk
		fetch => {
			'mpd/trunk' => 'trunk',
		},
		# svn-remote.svn.tags=mpd/tags/*:tags/*
		tags => {
			path => {
				left => 'mpd/tags',
				right => '',
				regex => qr!mpd/tags/([^/]+)$!,
				glob => 'tags/*',
			},
			ref => {
				left => 'tags',
				right => '',
				regex => qr!tags/([^/]+)$!,
				glob => 'tags/*',
			},
		}
	}
};

$log_entry hashref as returned by libsvn_log_entry()
{
	log => 'whitespace-formatted log entry
',						# trailing newline is preserved
	revision => '8',			# integer
	date => '2004-02-24T17:01:44.108345Z',	# commit date
	author => 'committer name'
};


# this is generated by generate_diff();
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
