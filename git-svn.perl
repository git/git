#!/usr/bin/env perl
# Copyright (C) 2006, Eric Wong <normalperson@yhbt.net>
# License: GPL v2 or later
use 5.008;
use warnings;
use strict;
use vars qw/	$AUTHOR $VERSION
		$sha1 $sha1_short $_revision $_repository
		$_q $_authors $_authors_prog %users/;
$AUTHOR = 'Eric Wong <normalperson@yhbt.net>';
$VERSION = '@@GIT_VERSION@@';

# From which subdir have we been invoked?
my $cmd_dir_prefix = eval {
	command_oneline([qw/rev-parse --show-prefix/], STDERR => 0)
} || '';

my $git_dir_user_set = 1 if defined $ENV{GIT_DIR};
$ENV{GIT_DIR} ||= '.git';
$Git::SVN::default_repo_id = 'svn';
$Git::SVN::default_ref_id = $ENV{GIT_SVN_ID} || 'git-svn';
$Git::SVN::Ra::_log_window_size = 100;
$Git::SVN::_minimize_url = 'unset';

if (! exists $ENV{SVN_SSH} && exists $ENV{GIT_SSH}) {
	$ENV{SVN_SSH} = $ENV{GIT_SSH};
}

if (exists $ENV{SVN_SSH} && $^O eq 'msys') {
	$ENV{SVN_SSH} =~ s/\\/\\\\/g;
	$ENV{SVN_SSH} =~ s/(.*)/"$1"/;
}

$Git::SVN::Log::TZ = $ENV{TZ};
$ENV{TZ} = 'UTC';
$| = 1; # unbuffer STDOUT

sub fatal (@) { print STDERR "@_\n"; exit 1 }
sub _req_svn {
	require SVN::Core; # use()-ing this causes segfaults for me... *shrug*
	require SVN::Ra;
	require SVN::Delta;
	if ($SVN::Core::VERSION lt '1.1.0') {
		fatal "Need SVN::Core 1.1.0 or better (got $SVN::Core::VERSION)";
	}
}
my $can_compress = eval { require Compress::Zlib; 1};
push @Git::SVN::Ra::ISA, 'SVN::Ra';
push @SVN::Git::Editor::ISA, 'SVN::Delta::Editor';
push @SVN::Git::Fetcher::ISA, 'SVN::Delta::Editor';
use Carp qw/croak/;
use Digest::MD5;
use IO::File qw//;
use File::Basename qw/dirname basename/;
use File::Path qw/mkpath/;
use File::Spec;
use File::Find;
use Getopt::Long qw/:config gnu_getopt no_ignore_case auto_abbrev/;
use IPC::Open3;
use Git;
use Memoize;  # core since 5.8.0, Jul 2002

BEGIN {
	# import functions from Git into our packages, en masse
	no strict 'refs';
	foreach (qw/command command_oneline command_noisy command_output_pipe
	            command_input_pipe command_close_pipe
	            command_bidi_pipe command_close_bidi_pipe/) {
		for my $package ( qw(SVN::Git::Editor SVN::Git::Fetcher
			Git::SVN::Migration Git::SVN::Log Git::SVN),
			__PACKAGE__) {
			*{"${package}::$_"} = \&{"Git::$_"};
		}
	}
	Memoize::memoize 'Git::config';
	Memoize::memoize 'Git::config_bool';
}

my ($SVN);

$sha1 = qr/[a-f\d]{40}/;
$sha1_short = qr/[a-f\d]{4,40}/;
my ($_stdin, $_help, $_edit,
	$_message, $_file, $_branch_dest,
	$_template, $_shared,
	$_version, $_fetch_all, $_no_rebase, $_fetch_parent,
	$_merge, $_strategy, $_dry_run, $_local,
	$_prefix, $_no_checkout, $_url, $_verbose,
	$_git_format, $_commit_url, $_tag, $_merge_info, $_interactive);
$Git::SVN::_follow_parent = 1;
$SVN::Git::Fetcher::_placeholder_filename = ".gitignore";
$_q ||= 0;
my %remote_opts = ( 'username=s' => \$Git::SVN::Prompt::_username,
                    'config-dir=s' => \$Git::SVN::Ra::config_dir,
                    'no-auth-cache' => \$Git::SVN::Prompt::_no_auth_cache,
                    'ignore-paths=s' => \$SVN::Git::Fetcher::_ignore_regex,
                    'ignore-refs=s' => \$Git::SVN::Ra::_ignore_refs_regex );
my %fc_opts = ( 'follow-parent|follow!' => \$Git::SVN::_follow_parent,
		'authors-file|A=s' => \$_authors,
		'authors-prog=s' => \$_authors_prog,
		'repack:i' => \$Git::SVN::_repack,
		'noMetadata' => \$Git::SVN::_no_metadata,
		'useSvmProps' => \$Git::SVN::_use_svm_props,
		'useSvnsyncProps' => \$Git::SVN::_use_svnsync_props,
		'log-window-size=i' => \$Git::SVN::Ra::_log_window_size,
		'no-checkout' => \$_no_checkout,
		'quiet|q+' => \$_q,
		'repack-flags|repack-args|repack-opts=s' =>
		   \$Git::SVN::_repack_flags,
		'use-log-author' => \$Git::SVN::_use_log_author,
		'add-author-from' => \$Git::SVN::_add_author_from,
		'localtime' => \$Git::SVN::_localtime,
		%remote_opts );

my ($_trunk, @_tags, @_branches, $_stdlayout);
my %icv;
my %init_opts = ( 'template=s' => \$_template, 'shared:s' => \$_shared,
                  'trunk|T=s' => \$_trunk, 'tags|t=s@' => \@_tags,
                  'branches|b=s@' => \@_branches, 'prefix=s' => \$_prefix,
                  'stdlayout|s' => \$_stdlayout,
                  'minimize-url|m!' => \$Git::SVN::_minimize_url,
		  'no-metadata' => sub { $icv{noMetadata} = 1 },
		  'use-svm-props' => sub { $icv{useSvmProps} = 1 },
		  'use-svnsync-props' => sub { $icv{useSvnsyncProps} = 1 },
		  'rewrite-root=s' => sub { $icv{rewriteRoot} = $_[1] },
		  'rewrite-uuid=s' => sub { $icv{rewriteUUID} = $_[1] },
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
			  'parent|p' => \$_fetch_parent,
			   %fc_opts } ],
	clone => [ \&cmd_clone, "Initialize and fetch revisions",
			{ 'revision|r=s' => \$_revision,
			  'preserve-empty-dirs' =>
				\$SVN::Git::Fetcher::_preserve_empty_dirs,
			  'placeholder-filename=s' =>
				\$SVN::Git::Fetcher::_placeholder_filename,
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
			  'commit-url=s' => \$_commit_url,
			  'revision|r=i' => \$_revision,
			  'no-rebase' => \$_no_rebase,
			  'mergeinfo=s' => \$_merge_info,
			  'interactive|i' => \$_interactive,
			%cmt_opts, %fc_opts } ],
	branch => [ \&cmd_branch,
	            'Create a branch in the SVN repository',
	            { 'message|m=s' => \$_message,
	              'destination|d=s' => \$_branch_dest,
	              'dry-run|n' => \$_dry_run,
	              'tag|t' => \$_tag,
	              'username=s' => \$Git::SVN::Prompt::_username,
	              'commit-url=s' => \$_commit_url } ],
	tag => [ sub { $_tag = 1; cmd_branch(@_) },
	         'Create a tag in the SVN repository',
	         { 'message|m=s' => \$_message,
	           'destination|d=s' => \$_branch_dest,
	           'dry-run|n' => \$_dry_run,
	           'username=s' => \$Git::SVN::Prompt::_username,
	           'commit-url=s' => \$_commit_url } ],
	'set-tree' => [ \&cmd_set_tree,
	                "Set an SVN repository to a git tree-ish",
			{ 'stdin' => \$_stdin, %cmt_opts, %fc_opts, } ],
	'create-ignore' => [ \&cmd_create_ignore,
			     'Create a .gitignore per svn:ignore',
			     { 'revision|r=i' => \$_revision
			     } ],
	'mkdirs' => [ \&cmd_mkdirs ,
	              "recreate empty directories after a checkout",
	              { 'revision|r=i' => \$_revision } ],
        'propget' => [ \&cmd_propget,
		       'Print the value of a property on a file or directory',
		       { 'revision|r=i' => \$_revision } ],
        'proplist' => [ \&cmd_proplist,
		       'List all properties of a file or directory',
		       { 'revision|r=i' => \$_revision } ],
	'show-ignore' => [ \&cmd_show_ignore, "Show svn:ignore listings",
			{ 'revision|r=i' => \$_revision
			} ],
	'show-externals' => [ \&cmd_show_externals, "Show svn:externals listings",
			{ 'revision|r=i' => \$_revision
			} ],
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
			  'pager=s' => \$Git::SVN::Log::pager
			} ],
	'find-rev' => [ \&cmd_find_rev,
	                "Translate between SVN revision numbers and tree-ish",
			{} ],
	'rebase' => [ \&cmd_rebase, "Fetch and rebase your working directory",
			{ 'merge|m|M' => \$_merge,
			  'verbose|v' => \$_verbose,
			  'strategy|s=s' => \$_strategy,
			  'local|l' => \$_local,
			  'fetch-all|all' => \$_fetch_all,
			  'dry-run|n' => \$_dry_run,
			  %fc_opts } ],
	'commit-diff' => [ \&cmd_commit_diff,
	                   'Commit a diff between two trees',
			{ 'message|m=s' => \$_message,
			  'file|F=s' => \$_file,
			  'revision|r=s' => \$_revision,
			%cmt_opts } ],
	'info' => [ \&cmd_info,
		    "Show info about the latest SVN revision
		     on the current branch",
		    { 'url' => \$_url, } ],
	'blame' => [ \&Git::SVN::Log::cmd_blame,
	            "Show what revision and author last modified each line of a file",
		    { 'git-format' => \$_git_format } ],
	'reset' => [ \&cmd_reset,
		     "Undo fetches back to the specified SVN revision",
		     { 'revision|r=s' => \$_revision,
		       'parent|p' => \$_fetch_parent } ],
	'gc' => [ \&cmd_gc,
		  "Compress unhandled.log files in .git/svn and remove " .
		  "index files in .git/svn",
		{} ],
);

use Term::ReadLine;
package FakeTerm;
sub new {
	my ($class, $reason) = @_;
	return bless \$reason, shift;
}
sub readline {
	my $self = shift;
	die "Cannot use readline on FakeTerm: $$self";
}
package main;

my $term = eval {
	$ENV{"GIT_SVN_NOTTY"}
		? new Term::ReadLine 'git-svn', \*STDIN, \*STDOUT
		: new Term::ReadLine 'git-svn';
};
if ($@) {
	$term = new FakeTerm "$@: going non-interactive";
}

my $cmd;
for (my $i = 0; $i < @ARGV; $i++) {
	if (defined $cmd{$ARGV[$i]}) {
		$cmd = $ARGV[$i];
		splice @ARGV, $i, 1;
		last;
	} elsif ($ARGV[$i] eq 'help') {
		$cmd = $ARGV[$i+1];
		usage(0);
	}
};

# make sure we're always running at the top-level working directory
unless ($cmd && $cmd =~ /(?:clone|init|multi-init)$/) {
	unless (-d $ENV{GIT_DIR}) {
		if ($git_dir_user_set) {
			die "GIT_DIR=$ENV{GIT_DIR} explicitly set, ",
			    "but it is not a directory\n";
		}
		my $git_dir = delete $ENV{GIT_DIR};
		my $cdup = undef;
		git_cmd_try {
			$cdup = command_oneline(qw/rev-parse --show-cdup/);
			$git_dir = '.' unless ($cdup);
			chomp $cdup if ($cdup);
			$cdup = "." unless ($cdup && length $cdup);
		} "Already at toplevel, but $git_dir not found\n";
		chdir $cdup or die "Unable to chdir up to '$cdup'\n";
		unless (-d $git_dir) {
			die "$git_dir still not found after going to ",
			    "'$cdup'\n";
		}
		$ENV{GIT_DIR} = $git_dir;
	}
	$_repository = Git->repository(Repository => $ENV{GIT_DIR});
}

my %opts = %{$cmd{$cmd}->[2]} if (defined $cmd);

read_git_config(\%opts);
if ($cmd && ($cmd eq 'log' || $cmd eq 'blame')) {
	Getopt::Long::Configure('pass_through');
}
my $rv = GetOptions(%opts, 'h|H' => \$_help, 'version|V' => \$_version,
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
if (defined $_authors_prog) {
	$_authors_prog = "'" . File::Spec->rel2abs($_authors_prog) . "'";
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
Usage: git svn <command> [options] [arguments]\n

	print $fd "Available commands:\n" unless $cmd;

	foreach (sort keys %cmd) {
		next if $cmd && $cmd ne $_;
		next if /^multi-/; # don't show deprecated commands
		print $fd '  ',pack('A17',$_),$cmd{$_}->[1],"\n";
		foreach (sort keys %{$cmd{$_}->[2]}) {
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
	::_req_svn();
	print "git-svn version $VERSION (svn $SVN::Core::VERSION)\n";
	exit 0;
}

sub ask {
	my ($prompt, %arg) = @_;
	my $valid_re = $arg{valid_re};
	my $default = $arg{default};
	my $resp;
	my $i = 0;

	if ( !( defined($term->IN)
            && defined( fileno($term->IN) )
            && defined( $term->OUT )
            && defined( fileno($term->OUT) ) ) ){
		return defined($default) ? $default : undef;
	}

	while ($i++ < 10) {
		$resp = $term->readline($prompt);
		if (!defined $resp) { # EOF
			print "\n";
			return defined $default ? $default : undef;
		}
		if ($resp eq '' and defined $default) {
			return $default;
		}
		if (!defined $valid_re or $resp =~ /$valid_re/) {
			return $resp;
		}
	}
	return undef;
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
		$_repository = Git->repository(Repository => ".git");
	}
	my $set;
	my $pfx = "svn-remote.$Git::SVN::default_repo_id";
	foreach my $i (keys %icv) {
		die "'$set' and '$i' cannot both be set\n" if $set;
		next unless defined $icv{$i};
		command_noisy('config', "$pfx.$i", $icv{$i});
		$set = $i;
	}
	my $ignore_paths_regex = \$SVN::Git::Fetcher::_ignore_regex;
	command_noisy('config', "$pfx.ignore-paths", $$ignore_paths_regex)
		if defined $$ignore_paths_regex;
	my $ignore_refs_regex = \$Git::SVN::Ra::_ignore_refs_regex;
	command_noisy('config', "$pfx.ignore-refs", $$ignore_refs_regex)
		if defined $$ignore_refs_regex;

	if (defined $SVN::Git::Fetcher::_preserve_empty_dirs) {
		my $fname = \$SVN::Git::Fetcher::_placeholder_filename;
		command_noisy('config', "$pfx.preserve-empty-dirs", 'true');
		command_noisy('config', "$pfx.placeholder-filename", $$fname);
	}
}

sub init_subdir {
	my $repo_path = shift or return;
	mkpath([$repo_path]) unless -d $repo_path;
	chdir $repo_path or die "Couldn't chdir to $repo_path: $!\n";
	$ENV{GIT_DIR} = '.git';
	$_repository = Git->repository(Repository => $ENV{GIT_DIR});
}

sub cmd_clone {
	my ($url, $path) = @_;
	if (!defined $path &&
	    (defined $_trunk || @_branches || @_tags ||
	     defined $_stdlayout) &&
	    $url !~ m#^[a-z\+]+://#) {
		$path = $url;
	}
	$path = basename($url) if !defined $path || !length $path;
	my $authors_absolute = $_authors ? File::Spec->rel2abs($_authors) : "";
	cmd_init($url, $path);
	command_oneline('config', 'svn.authorsfile', $authors_absolute)
	    if $_authors;
	Git::SVN::fetch_all($Git::SVN::default_repo_id);
}

sub cmd_init {
	if (defined $_stdlayout) {
		$_trunk = 'trunk' if (!defined $_trunk);
		@_tags = 'tags' if (! @_tags);
		@_branches = 'branches' if (! @_branches);
	}
	if (defined $_trunk || @_branches || @_tags) {
		return cmd_multi_init(@_);
	}
	my $url = shift or die "SVN repository location required ",
	                       "as a command-line argument\n";
	$url = canonicalize_url($url);
	init_subdir(@_);
	do_git_init_db();

	if ($Git::SVN::_minimize_url eq 'unset') {
		$Git::SVN::_minimize_url = 0;
	}

	Git::SVN->init($url);
}

sub cmd_fetch {
	if (grep /^\d+=./, @_) {
		die "'<rev>=<commit>' fetch arguments are ",
		    "no longer supported.\n";
	}
	my ($remote) = @_;
	if (@_ > 1) {
		die "Usage: $0 fetch [--all] [--parent] [svn-remote]\n";
	}
	$Git::SVN::no_reuse_existing = undef;
	if ($_fetch_parent) {
		my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
		unless ($gs) {
			die "Unable to determine upstream SVN information from ",
			    "working tree history\n";
		}
	        # just fetch, don't checkout.
		$_no_checkout = 'true';
		$_fetch_all ? $gs->fetch_all : $gs->fetch;
	} elsif ($_fetch_all) {
		cmd_multi_fetch();
	} else {
		$remote ||= $Git::SVN::default_repo_id;
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
			fatal "Failed to rev-parse $c";
		}
	}
	my $gs = Git::SVN->new;
	my ($r_last, $cmt_last) = $gs->last_rev_commit;
	$gs->fetch;
	if (defined $gs->{last_rev} && $r_last != $gs->{last_rev}) {
		fatal "There are new revisions that were fetched ",
		      "and need to be merged (or acknowledged) ",
		      "before committing.\nlast rev: $r_last\n",
		      " current: $gs->{last_rev}";
	}
	$gs->set_tree($_) foreach @revs;
	print "Done committing ",scalar @revs," revisions to SVN\n";
	unlink $gs->{index};
}

sub split_merge_info_range {
	my ($range) = @_;
	if ($range =~ /(\d+)-(\d+)/) {
		return (int($1), int($2));
	} else {
		return (int($range), int($range));
	}
}

sub combine_ranges {
	my ($in) = @_;

	my @fnums = ();
	my @arr = split(/,/, $in);
	for my $element (@arr) {
		my ($start, $end) = split_merge_info_range($element);
		push @fnums, $start;
	}

	my @sorted = @arr [ sort {
		$fnums[$a] <=> $fnums[$b]
	} 0..$#arr ];

	my @return = ();
	my $last = -1;
	my $first = -1;
	for my $element (@sorted) {
		my ($start, $end) = split_merge_info_range($element);

		if ($last == -1) {
			$first = $start;
			$last = $end;
			next;
		}
		if ($start <= $last+1) {
			if ($end > $last) {
				$last = $end;
			}
			next;
		}
		if ($first == $last) {
			push @return, "$first";
		} else {
			push @return, "$first-$last";
		}
		$first = $start;
		$last = $end;
	}

	if ($first != -1) {
		if ($first == $last) {
			push @return, "$first";
		} else {
			push @return, "$first-$last";
		}
	}

	return join(',', @return);
}

sub merge_revs_into_hash {
	my ($hash, $minfo) = @_;
	my @lines = split(' ', $minfo);

	for my $line (@lines) {
		my ($branchpath, $revs) = split(/:/, $line);

		if (exists($hash->{$branchpath})) {
			# Merge the two revision sets
			my $combined = "$hash->{$branchpath},$revs";
			$hash->{$branchpath} = combine_ranges($combined);
		} else {
			# Just do range combining for consolidation
			$hash->{$branchpath} = combine_ranges($revs);
		}
	}
}

sub merge_merge_info {
	my ($mergeinfo_one, $mergeinfo_two) = @_;
	my %result_hash = ();

	merge_revs_into_hash(\%result_hash, $mergeinfo_one);
	merge_revs_into_hash(\%result_hash, $mergeinfo_two);

	my $result = '';
	# Sort below is for consistency's sake
	for my $branchname (sort keys(%result_hash)) {
		my $revlist = $result_hash{$branchname};
		$result .= "$branchname:$revlist\n"
	}
	return $result;
}

sub populate_merge_info {
	my ($d, $gs, $uuid, $linear_refs, $rewritten_parent) = @_;

	my %parentshash;
	read_commit_parents(\%parentshash, $d);
	my @parents = @{$parentshash{$d}};
	if ($#parents > 0) {
		# Merge commit
		my $all_parents_ok = 1;
		my $aggregate_mergeinfo = '';
		my $rooturl = $gs->repos_root;

		if (defined($rewritten_parent)) {
			# Replace first parent with newly-rewritten version
			shift @parents;
			unshift @parents, $rewritten_parent;
		}

		foreach my $parent (@parents) {
			my ($branchurl, $svnrev, $paruuid) =
				cmt_metadata($parent);

			unless (defined($svnrev)) {
				# Should have been caught be preflight check
				fatal "merge commit $d has ancestor $parent, but that change "
                     ."does not have git-svn metadata!";
			}
			unless ($branchurl =~ /^\Q$rooturl\E(.*)/) {
				fatal "commit $parent git-svn metadata changed mid-run!";
			}
			my $branchpath = $1;

			my $ra = Git::SVN::Ra->new($branchurl);
			my (undef, undef, $props) =
				$ra->get_dir(canonicalize_path("."), $svnrev);
			my $par_mergeinfo = $props->{'svn:mergeinfo'};
			unless (defined $par_mergeinfo) {
				$par_mergeinfo = '';
			}
			# Merge previous mergeinfo values
			$aggregate_mergeinfo =
				merge_merge_info($aggregate_mergeinfo,
								 $par_mergeinfo, 0);

			next if $parent eq $parents[0]; # Skip first parent
			# Add new changes being placed in tree by merge
			my @cmd = (qw/rev-list --reverse/,
					   $parent, qw/--not/);
			foreach my $par (@parents) {
				unless ($par eq $parent) {
					push @cmd, $par;
				}
			}
			my @revsin = ();
			my ($revlist, $ctx) = command_output_pipe(@cmd);
			while (<$revlist>) {
				my $irev = $_;
				chomp $irev;
				my (undef, $csvnrev, undef) =
					cmt_metadata($irev);
				unless (defined $csvnrev) {
					# A child is missing SVN annotations...
					# this might be OK, or might not be.
					warn "W:child $irev is merged into revision "
						 ."$d but does not have git-svn metadata. "
						 ."This means git-svn cannot determine the "
						 ."svn revision numbers to place into the "
						 ."svn:mergeinfo property. You must ensure "
						 ."a branch is entirely committed to "
						 ."SVN before merging it in order for "
						 ."svn:mergeinfo population to function "
						 ."properly";
				}
				push @revsin, $csvnrev;
			}
			command_close_pipe($revlist, $ctx);

			last unless $all_parents_ok;

			# We now have a list of all SVN revnos which are
			# merged by this particular parent. Integrate them.
			next if $#revsin == -1;
			my $newmergeinfo = "$branchpath:" . join(',', @revsin);
			$aggregate_mergeinfo =
				merge_merge_info($aggregate_mergeinfo,
								 $newmergeinfo, 1);
		}
		if ($all_parents_ok and $aggregate_mergeinfo) {
			return $aggregate_mergeinfo;
		}
	}

	return undef;
}

sub cmd_dcommit {
	my $head = shift;
	command_noisy(qw/update-index --refresh/);
	git_cmd_try { command_oneline(qw/diff-index --quiet HEAD/) }
		'Cannot dcommit with a dirty index.  Commit your changes first, '
		. "or stash them with `git stash'.\n";
	$head ||= 'HEAD';

	my $old_head;
	if ($head ne 'HEAD') {
		$old_head = eval {
			command_oneline([qw/symbolic-ref -q HEAD/])
		};
		if ($old_head) {
			$old_head =~ s{^refs/heads/}{};
		} else {
			$old_head = eval { command_oneline(qw/rev-parse HEAD/) };
		}
		command(['checkout', $head], STDERR => 0);
	}

	my @refs;
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD', \@refs);
	unless ($gs) {
		die "Unable to determine upstream SVN information from ",
		    "$head history.\nPerhaps the repository is empty.";
	}

	if (defined $_commit_url) {
		$url = $_commit_url;
	} else {
		$url = eval { command_oneline('config', '--get',
			      "svn-remote.$gs->{repo_id}.commiturl") };
		if (!$url) {
			$url = $gs->full_pushurl
		}
	}

	my $last_rev = $_revision if defined $_revision;
	if ($url) {
		print "Committing to $url ...\n";
	}
	my ($linear_refs, $parents) = linearize_history($gs, \@refs);
	if ($_no_rebase && scalar(@$linear_refs) > 1) {
		warn "Attempting to commit more than one change while ",
		     "--no-rebase is enabled.\n",
		     "If these changes depend on each other, re-running ",
		     "without --no-rebase may be required."
	}

	if (defined $_interactive){
		my $ask_default = "y";
		foreach my $d (@$linear_refs){
			my ($fh, $ctx) = command_output_pipe(qw(show --summary), "$d");
			while (<$fh>){
				print $_;
			}
			command_close_pipe($fh, $ctx);
			$_ = ask("Commit this patch to SVN? ([y]es (default)|[n]o|[q]uit|[a]ll): ",
			         valid_re => qr/^(?:yes|y|no|n|quit|q|all|a)/i,
			         default => $ask_default);
			die "Commit this patch reply required" unless defined $_;
			if (/^[nq]/i) {
				exit(0);
			} elsif (/^a/i) {
				last;
			}
		}
	}

	my $expect_url = $url;

	my $push_merge_info = eval {
		command_oneline(qw/config --get svn.pushmergeinfo/)
		};
	if (not defined($push_merge_info)
			or $push_merge_info eq "false"
			or $push_merge_info eq "no"
			or $push_merge_info eq "never") {
		$push_merge_info = 0;
	}

	unless (defined($_merge_info) || ! $push_merge_info) {
		# Preflight check of changes to ensure no issues with mergeinfo
		# This includes check for uncommitted-to-SVN parents
		# (other than the first parent, which we will handle),
		# information from different SVN repos, and paths
		# which are not underneath this repository root.
		my $rooturl = $gs->repos_root;
		foreach my $d (@$linear_refs) {
			my %parentshash;
			read_commit_parents(\%parentshash, $d);
			my @realparents = @{$parentshash{$d}};
			if ($#realparents > 0) {
				# Merge commit
				shift @realparents; # Remove/ignore first parent
				foreach my $parent (@realparents) {
					my ($branchurl, $svnrev, $paruuid) = cmt_metadata($parent);
					unless (defined $paruuid) {
						# A parent is missing SVN annotations...
						# abort the whole operation.
						fatal "$parent is merged into revision $d, "
							 ."but does not have git-svn metadata. "
							 ."Either dcommit the branch or use a "
							 ."local cherry-pick, FF merge, or rebase "
							 ."instead of an explicit merge commit.";
					}

					unless ($paruuid eq $uuid) {
						# Parent has SVN metadata from different repository
						fatal "merge parent $parent for change $d has "
							 ."git-svn uuid $paruuid, while current change "
							 ."has uuid $uuid!";
					}

					unless ($branchurl =~ /^\Q$rooturl\E(.*)/) {
						# This branch is very strange indeed.
						fatal "merge parent $parent for $d is on branch "
							 ."$branchurl, which is not under the "
							 ."git-svn root $rooturl!";
					}
				}
			}
		}
	}

	my $rewritten_parent;
	Git::SVN::remove_username($expect_url);
	if (defined($_merge_info)) {
		$_merge_info =~ tr{ }{\n};
	}
	while (1) {
		my $d = shift @$linear_refs or last;
		unless (defined $last_rev) {
			(undef, $last_rev, undef) = cmt_metadata("$d~1");
			unless (defined $last_rev) {
				fatal "Unable to extract revision information ",
				      "from commit $d~1";
			}
		}
		if ($_dry_run) {
			print "diff-tree $d~1 $d\n";
		} else {
			my $cmt_rev;

			unless (defined($_merge_info) || ! $push_merge_info) {
				$_merge_info = populate_merge_info($d, $gs,
				                             $uuid,
				                             $linear_refs,
				                             $rewritten_parent);
			}

			my %ed_opts = ( r => $last_rev,
			                log => get_commit_entry($d)->{log},
			                ra => Git::SVN::Ra->new($url),
			                config => SVN::Core::config_get_config(
			                        $Git::SVN::Ra::config_dir
			                ),
			                tree_a => "$d~1",
			                tree_b => $d,
			                editor_cb => sub {
			                       print "Committed r$_[0]\n";
			                       $cmt_rev = $_[0];
			                },
					mergeinfo => $_merge_info,
			                svn_path => '');
			if (!SVN::Git::Editor->new(\%ed_opts)->apply_diff) {
				print "No changes\n$d~1 == $d\n";
			} elsif ($parents->{$d} && @{$parents->{$d}}) {
				$gs->{inject_parents_dcommit}->{$cmt_rev} =
				                               $parents->{$d};
			}
			$_fetch_all ? $gs->fetch_all : $gs->fetch;
			$last_rev = $cmt_rev;
			next if $_no_rebase;

			# we always want to rebase against the current HEAD,
			# not any head that was passed to us
			my @diff = command('diff-tree', $d,
			                   $gs->refname, '--');
			my @finish;
			if (@diff) {
				@finish = rebase_cmd();
				print STDERR "W: $d and ", $gs->refname,
				             " differ, using @finish:\n",
				             join("\n", @diff), "\n";
			} else {
				print "No changes between current HEAD and ",
				      $gs->refname,
				      "\nResetting to the latest ",
				      $gs->refname, "\n";
				@finish = qw/reset --mixed/;
			}
			command_noisy(@finish, $gs->refname);

			$rewritten_parent = command_oneline(qw/rev-parse HEAD/);

			if (@diff) {
				@refs = ();
				my ($url_, $rev_, $uuid_, $gs_) =
				              working_head_info('HEAD', \@refs);
				my ($linear_refs_, $parents_) =
				              linearize_history($gs_, \@refs);
				if (scalar(@$linear_refs) !=
				    scalar(@$linear_refs_)) {
					fatal "# of revisions changed ",
					  "\nbefore:\n",
					  join("\n", @$linear_refs),
					  "\n\nafter:\n",
					  join("\n", @$linear_refs_), "\n",
					  'If you are attempting to commit ',
					  "merges, try running:\n\t",
					  'git rebase --interactive',
					  '--preserve-merges ',
					  $gs->refname,
					  "\nBefore dcommitting";
				}
				if ($url_ ne $expect_url) {
					if ($url_ eq $gs->metadata_url) {
						print
						  "Accepting rewritten URL:",
						  " $url_\n";
					} else {
						fatal
						  "URL mismatch after rebase:",
						  " $url_ != $expect_url";
					}
				}
				if ($uuid_ ne $uuid) {
					fatal "uuid mismatch after rebase: ",
					      "$uuid_ != $uuid";
				}
				# remap parents
				my (%p, @l, $i);
				for ($i = 0; $i < scalar @$linear_refs; $i++) {
					my $new = $linear_refs_->[$i] or next;
					$p{$new} =
						$parents->{$linear_refs->[$i]};
					push @l, $new;
				}
				$parents = \%p;
				$linear_refs = \@l;
			}
		}
	}

	if ($old_head) {
		my $new_head = command_oneline(qw/rev-parse HEAD/);
		my $new_is_symbolic = eval {
			command_oneline(qw/symbolic-ref -q HEAD/);
		};
		if ($new_is_symbolic) {
			print "dcommitted the branch ", $head, "\n";
		} else {
			print "dcommitted on a detached HEAD because you gave ",
			      "a revision argument.\n",
			      "The rewritten commit is: ", $new_head, "\n";
		}
		command(['checkout', $old_head], STDERR => 0);
	}

	unlink $gs->{index};
}

sub cmd_branch {
	my ($branch_name, $head) = @_;

	unless (defined $branch_name && length $branch_name) {
		die(($_tag ? "tag" : "branch") . " name required\n");
	}
	$head ||= 'HEAD';

	my (undef, $rev, undef, $gs) = working_head_info($head);
	my $src = $gs->full_pushurl;

	my $remote = Git::SVN::read_all_remotes()->{$gs->{repo_id}};
	my $allglobs = $remote->{ $_tag ? 'tags' : 'branches' };
	my $glob;
	if ($#{$allglobs} == 0) {
		$glob = $allglobs->[0];
	} else {
		unless(defined $_branch_dest) {
			die "Multiple ",
			    $_tag ? "tag" : "branch",
			    " paths defined for Subversion repository.\n",
		            "You must specify where you want to create the ",
		            $_tag ? "tag" : "branch",
		            " with the --destination argument.\n";
		}
		foreach my $g (@{$allglobs}) {
			# SVN::Git::Editor could probably be moved to Git.pm..
			my $re = SVN::Git::Editor::glob2pat($g->{path}->{left});
			if ($_branch_dest =~ /$re/) {
				$glob = $g;
				last;
			}
		}
		unless (defined $glob) {
			my $dest_re = qr/\b\Q$_branch_dest\E\b/;
			foreach my $g (@{$allglobs}) {
				$g->{path}->{left} =~ /$dest_re/ or next;
				if (defined $glob) {
					die "Ambiguous destination: ",
					    $_branch_dest, "\nmatches both '",
					    $glob->{path}->{left}, "' and '",
					    $g->{path}->{left}, "'\n";
				}
				$glob = $g;
			}
			unless (defined $glob) {
				die "Unknown ",
				    $_tag ? "tag" : "branch",
				    " destination $_branch_dest\n";
			}
		}
	}
	my ($lft, $rgt) = @{ $glob->{path} }{qw/left right/};
	my $url;
	if (defined $_commit_url) {
		$url = $_commit_url;
	} else {
		$url = eval { command_oneline('config', '--get',
			"svn-remote.$gs->{repo_id}.commiturl") };
		if (!$url) {
			$url = $remote->{pushurl} || $remote->{url};
		}
	}
	my $dst = join '/', $url, $lft, $branch_name, ($rgt || ());

	if ($dst =~ /^https:/ && $src =~ /^http:/) {
		$src=~s/^http:/https:/;
	}

	::_req_svn();

	my $ctx = SVN::Client->new(
		auth    => Git::SVN::Ra::_auth_providers(),
		log_msg => sub {
			${ $_[0] } = defined $_message
				? $_message
				: 'Create ' . ($_tag ? 'tag ' : 'branch ' )
				. $branch_name;
		},
	);

	eval {
		$ctx->ls($dst, 'HEAD', 0);
	} and die "branch ${branch_name} already exists\n";

	print "Copying ${src} at r${rev} to ${dst}...\n";
	$ctx->copy($src, $rev, $dst)
		unless $_dry_run;

	$gs->fetch_all;
}

sub cmd_find_rev {
	my $revision_or_hash = shift or die "SVN or git revision required ",
	                                    "as a command-line argument\n";
	my $result;
	if ($revision_or_hash =~ /^r\d+$/) {
		my $head = shift;
		$head ||= 'HEAD';
		my @refs;
		my (undef, undef, $uuid, $gs) = working_head_info($head, \@refs);
		unless ($gs) {
			die "Unable to determine upstream SVN information from ",
			    "$head history\n";
		}
		my $desired_revision = substr($revision_or_hash, 1);
		$result = $gs->rev_map_get($desired_revision, $uuid);
	} else {
		my (undef, $rev, undef) = cmt_metadata($revision_or_hash);
		$result = $rev;
	}
	print "$result\n" if $result;
}

sub auto_create_empty_directories {
	my ($gs) = @_;
	my $var = eval { command_oneline('config', '--get', '--bool',
					 "svn-remote.$gs->{repo_id}.automkdirs") };
	# By default, create empty directories by consulting the unhandled log,
	# but allow setting it to 'false' to skip it.
	return !($var && $var eq 'false');
}

sub cmd_rebase {
	command_noisy(qw/update-index --refresh/);
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	unless ($gs) {
		die "Unable to determine upstream SVN information from ",
		    "working tree history\n";
	}
	if ($_dry_run) {
		print "Remote Branch: " . $gs->refname . "\n";
		print "SVN URL: " . $url . "\n";
		return;
	}
	if (command(qw/diff-index HEAD --/)) {
		print STDERR "Cannot rebase with uncommited changes:\n";
		command_noisy('status');
		exit 1;
	}
	unless ($_local) {
		# rebase will checkout for us, so no need to do it explicitly
		$_no_checkout = 'true';
		$_fetch_all ? $gs->fetch_all : $gs->fetch;
	}
	command_noisy(rebase_cmd(), $gs->refname);
	if (auto_create_empty_directories($gs)) {
		$gs->mkemptydirs;
	}
}

sub cmd_show_ignore {
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	$gs ||= Git::SVN->new;
	my $r = (defined $_revision ? $_revision : $gs->ra->get_latest_revnum);
	$gs->prop_walk($gs->{path}, $r, sub {
		my ($gs, $path, $props) = @_;
		print STDOUT "\n# $path\n";
		my $s = $props->{'svn:ignore'} or return;
		$s =~ s/[\r\n]+/\n/g;
		$s =~ s/^\n+//;
		chomp $s;
		$s =~ s#^#$path#gm;
		print STDOUT "$s\n";
	});
}

sub cmd_show_externals {
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	$gs ||= Git::SVN->new;
	my $r = (defined $_revision ? $_revision : $gs->ra->get_latest_revnum);
	$gs->prop_walk($gs->{path}, $r, sub {
		my ($gs, $path, $props) = @_;
		print STDOUT "\n# $path\n";
		my $s = $props->{'svn:externals'} or return;
		$s =~ s/[\r\n]+/\n/g;
		chomp $s;
		$s =~ s#^#$path#gm;
		print STDOUT "$s\n";
	});
}

sub cmd_create_ignore {
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	$gs ||= Git::SVN->new;
	my $r = (defined $_revision ? $_revision : $gs->ra->get_latest_revnum);
	$gs->prop_walk($gs->{path}, $r, sub {
		my ($gs, $path, $props) = @_;
		# $path is of the form /path/to/dir/
		$path = '.' . $path;
		# SVN can have attributes on empty directories,
		# which git won't track
		mkpath([$path]) unless -d $path;
		my $ignore = $path . '.gitignore';
		my $s = $props->{'svn:ignore'} or return;
		open(GITIGNORE, '>', $ignore)
		  or fatal("Failed to open `$ignore' for writing: $!");
		$s =~ s/[\r\n]+/\n/g;
		$s =~ s/^\n+//;
		chomp $s;
		# Prefix all patterns so that the ignore doesn't apply
		# to sub-directories.
		$s =~ s#^#/#gm;
		print GITIGNORE "$s\n";
		close(GITIGNORE)
		  or fatal("Failed to close `$ignore': $!");
		command_noisy('add', '-f', $ignore);
	});
}

sub cmd_mkdirs {
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	$gs ||= Git::SVN->new;
	$gs->mkemptydirs($_revision);
}

sub canonicalize_path {
	my ($path) = @_;
	my $dot_slash_added = 0;
	if (substr($path, 0, 1) ne "/") {
		$path = "./" . $path;
		$dot_slash_added = 1;
	}
	# File::Spec->canonpath doesn't collapse x/../y into y (for a
	# good reason), so let's do this manually.
	$path =~ s#/+#/#g;
	$path =~ s#/\.(?:/|$)#/#g;
	$path =~ s#/[^/]+/\.\.##g;
	$path =~ s#/$##g;
	$path =~ s#^\./## if $dot_slash_added;
	$path =~ s#^/##;
	$path =~ s#^\.$##;
	return $path;
}

sub canonicalize_url {
	my ($url) = @_;
	$url =~ s#^([^:]+://[^/]*/)(.*)$#$1 . canonicalize_path($2)#e;
	return $url;
}

# get_svnprops(PATH)
# ------------------
# Helper for cmd_propget and cmd_proplist below.
sub get_svnprops {
	my $path = shift;
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	$gs ||= Git::SVN->new;

	# prefix THE PATH by the sub-directory from which the user
	# invoked us.
	$path = $cmd_dir_prefix . $path;
	fatal("No such file or directory: $path") unless -e $path;
	my $is_dir = -d $path ? 1 : 0;
	$path = $gs->{path} . '/' . $path;

	# canonicalize the path (otherwise libsvn will abort or fail to
	# find the file)
	$path = canonicalize_path($path);

	my $r = (defined $_revision ? $_revision : $gs->ra->get_latest_revnum);
	my $props;
	if ($is_dir) {
		(undef, undef, $props) = $gs->ra->get_dir($path, $r);
	}
	else {
		(undef, $props) = $gs->ra->get_file($path, $r, undef);
	}
	return $props;
}

# cmd_propget (PROP, PATH)
# ------------------------
# Print the SVN property PROP for PATH.
sub cmd_propget {
	my ($prop, $path) = @_;
	$path = '.' if not defined $path;
	usage(1) if not defined $prop;
	my $props = get_svnprops($path);
	if (not defined $props->{$prop}) {
		fatal("`$path' does not have a `$prop' SVN property.");
	}
	print $props->{$prop} . "\n";
}

# cmd_proplist (PATH)
# -------------------
# Print the list of SVN properties for PATH.
sub cmd_proplist {
	my $path = shift;
	$path = '.' if not defined $path;
	my $props = get_svnprops($path);
	print "Properties on '$path':\n";
	foreach (sort keys %{$props}) {
		print "  $_\n";
	}
}

sub cmd_multi_init {
	my $url = shift;
	unless (defined $_trunk || @_branches || @_tags) {
		usage(1);
	}

	$_prefix = '' unless defined $_prefix;
	if (defined $url) {
		$url = canonicalize_url($url);
		init_subdir(@_);
	}
	do_git_init_db();
	if (defined $_trunk) {
		$_trunk =~ s#^/+##;
		my $trunk_ref = 'refs/remotes/' . $_prefix . 'trunk';
		# try both old-style and new-style lookups:
		my $gs_trunk = eval { Git::SVN->new($trunk_ref) };
		unless ($gs_trunk) {
			my ($trunk_url, $trunk_path) =
			                      complete_svn_url($url, $_trunk);
			$gs_trunk = Git::SVN->init($trunk_url, $trunk_path,
						   undef, $trunk_ref);
		}
	}
	return unless @_branches || @_tags;
	my $ra = $url ? Git::SVN::Ra->new($url) : undef;
	foreach my $path (@_branches) {
		complete_url_ls_init($ra, $path, '--branches/-b', $_prefix);
	}
	foreach my $path (@_tags) {
		complete_url_ls_init($ra, $path, '--tags/-t', $_prefix.'tags/');
	}
}

sub cmd_multi_fetch {
	$Git::SVN::no_reuse_existing = undef;
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
	            "<tree-ish> <tree-ish> [<URL>]";
	fatal($usage) if (!defined $ta || !defined $tb);
	my $svn_path = '';
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
		      "I have no idea what you mean");
	}
	if (defined $_file) {
		$_message = file_to_s($_file);
	} else {
		$_message ||= get_commit_entry($tb)->{log};
	}
	my $ra ||= Git::SVN::Ra->new($url);
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

sub escape_uri_only {
	my ($uri) = @_;
	my @tmp;
	foreach (split m{/}, $uri) {
		s/([^~\w.%+-]|%(?![a-fA-F0-9]{2}))/sprintf("%%%02X",ord($1))/eg;
		push @tmp, $_;
	}
	join('/', @tmp);
}

sub escape_url {
	my ($url) = @_;
	if ($url =~ m#^([^:]+)://([^/]*)(.*)$#) {
		my ($scheme, $domain, $uri) = ($1, $2, escape_uri_only($3));
		$url = "$scheme://$domain$uri";
	}
	$url;
}

sub cmd_info {
	my $path = canonicalize_path(defined($_[0]) ? $_[0] : ".");
	my $fullpath = canonicalize_path($cmd_dir_prefix . $path);
	if (exists $_[1]) {
		die "Too many arguments specified\n";
	}

	my ($file_type, $diff_status) = find_file_type_and_diff_status($path);

	if (!$file_type && !$diff_status) {
		print STDERR "svn: '$path' is not under version control\n";
		exit 1;
	}

	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	unless ($gs) {
		die "Unable to determine upstream SVN information from ",
		    "working tree history\n";
	}

	# canonicalize_path() will return "" to make libsvn 1.5.x happy,
	$path = "." if $path eq "";

	my $full_url = $url . ($fullpath eq "" ? "" : "/$fullpath");

	if ($_url) {
		print escape_url($full_url), "\n";
		return;
	}

	my $result = "Path: $path\n";
	$result .= "Name: " . basename($path) . "\n" if $file_type ne "dir";
	$result .= "URL: " . escape_url($full_url) . "\n";

	eval {
		my $repos_root = $gs->repos_root;
		Git::SVN::remove_username($repos_root);
		$result .= "Repository Root: " . escape_url($repos_root) . "\n";
	};
	if ($@) {
		$result .= "Repository Root: (offline)\n";
	}
	::_req_svn();
	$result .= "Repository UUID: $uuid\n" unless $diff_status eq "A" &&
		($SVN::Core::VERSION le '1.5.4' || $file_type ne "dir");
	$result .= "Revision: " . ($diff_status eq "A" ? 0 : $rev) . "\n";

	$result .= "Node Kind: " .
		   ($file_type eq "dir" ? "directory" : "file") . "\n";

	my $schedule = $diff_status eq "A"
		       ? "add"
		       : ($diff_status eq "D" ? "delete" : "normal");
	$result .= "Schedule: $schedule\n";

	if ($diff_status eq "A") {
		print $result, "\n";
		return;
	}

	my ($lc_author, $lc_rev, $lc_date_utc);
	my @args = Git::SVN::Log::git_svn_log_cmd($rev, $rev, "--", $fullpath);
	my $log = command_output_pipe(@args);
	my $esc_color = qr/(?:\033\[(?:(?:\d+;)*\d*)?m)*/;
	while (<$log>) {
		if (/^${esc_color}author (.+) <[^>]+> (\d+) ([\-\+]?\d+)$/o) {
			$lc_author = $1;
			$lc_date_utc = Git::SVN::Log::parse_git_date($2, $3);
		} elsif (/^${esc_color}    (git-svn-id:.+)$/o) {
			(undef, $lc_rev, undef) = ::extract_metadata($1);
		}
	}
	close $log;

	Git::SVN::Log::set_local_timezone();

	$result .= "Last Changed Author: $lc_author\n";
	$result .= "Last Changed Rev: $lc_rev\n";
	$result .= "Last Changed Date: " .
		   Git::SVN::Log::format_svn_date($lc_date_utc) . "\n";

	if ($file_type ne "dir") {
		my $text_last_updated_date =
		    ($diff_status eq "D" ? $lc_date_utc : (stat $path)[9]);
		$result .=
		    "Text Last Updated: " .
		    Git::SVN::Log::format_svn_date($text_last_updated_date) .
		    "\n";
		my $checksum;
		if ($diff_status eq "D") {
			my ($fh, $ctx) =
			    command_output_pipe(qw(cat-file blob), "HEAD:$path");
			if ($file_type eq "link") {
				my $file_name = <$fh>;
				$checksum = md5sum("link $file_name");
			} else {
				$checksum = md5sum($fh);
			}
			command_close_pipe($fh, $ctx);
		} elsif ($file_type eq "link") {
			my $file_name =
			    command(qw(cat-file blob), "HEAD:$path");
			$checksum =
			    md5sum("link " . $file_name);
		} else {
			open FILE, "<", $path or die $!;
			$checksum = md5sum(\*FILE);
			close FILE or die $!;
		}
		$result .= "Checksum: " . $checksum . "\n";
	}

	print $result, "\n";
}

sub cmd_reset {
	my $target = shift || $_revision or die "SVN revision required\n";
	$target = $1 if $target =~ /^r(\d+)$/;
	$target =~ /^\d+$/ or die "Numeric SVN revision expected\n";
	my ($url, $rev, $uuid, $gs) = working_head_info('HEAD');
	unless ($gs) {
		die "Unable to determine upstream SVN information from ".
		    "history\n";
	}
	my ($r, $c) = $gs->find_rev_before($target, not $_fetch_parent);
	die "Cannot find SVN revision $target\n" unless defined($c);
	$gs->rev_map_set($r, $c, 'reset', $uuid);
	print "r$r = $c ($gs->{ref_id})\n";
}

sub cmd_gc {
	if (!$can_compress) {
		warn "Compress::Zlib could not be found; unhandled.log " .
		     "files will not be compressed.\n";
	}
	find({ wanted => \&gc_directory, no_chdir => 1}, "$ENV{GIT_DIR}/svn");
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

	# look for "trunk" ref if it exists
	my $remote = Git::SVN::read_all_remotes()->{$gs->{repo_id}};
	my $fetch = $remote->{fetch};
	if ($fetch) {
		foreach my $p (keys %$fetch) {
			basename($fetch->{$p}) eq 'trunk' or next;
			$gs = Git::SVN->new($fetch->{$p}, $gs->{repo_id}, $p);
			last;
		}
	}

	my $valid_head = verify_ref('HEAD^0');
	command_noisy(qw(update-ref refs/heads/master), $gs->refname);
	return if ($valid_head || !verify_ref('HEAD^0'));

	return if $ENV{GIT_DIR} !~ m#^(?:.*/)?\.git$#;
	my $index = $ENV{GIT_INDEX_FILE} || "$ENV{GIT_DIR}/index";
	return if -f $index;

	return if command_oneline(qw/rev-parse --is-inside-work-tree/) eq 'false';
	return if command_oneline(qw/rev-parse --is-inside-git-dir/) eq 'true';
	command_noisy(qw/read-tree -m -u -v HEAD HEAD/);
	print STDERR "Checked out HEAD:\n  ",
	             $gs->full_url, " r", $gs->last_rev, "\n";
	if (auto_create_empty_directories($gs)) {
		$gs->mkemptydirs($gs->last_rev);
	}
}

sub complete_svn_url {
	my ($url, $path) = @_;
	$path =~ s#/+$##;
	if ($path !~ m#^[a-z\+]+://#) {
		if (!defined $url || $url !~ m#^[a-z\+]+://#) {
			fatal("E: '$path' is not a complete URL ",
			      "and a separate URL is not specified");
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
			      "and a separate URL is not specified");
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
	my $remote_path = "$gs->{path}/$repo_path";
	$remote_path =~ s{%([0-9A-F]{2})}{chr hex($1)}ieg;
	$remote_path =~ s#/+#/#g;
	$remote_path =~ s#^/##g;
	$remote_path .= "/*" if $remote_path !~ /\*/;
	my ($n) = ($switch =~ /^--(\w+)/);
	if (length $pfx && $pfx !~ m#/$#) {
		die "--prefix='$pfx' must have a trailing slash '/'\n";
	}
	command_noisy('config',
		      '--add',
	              "svn-remote.$gs->{repo_id}.$n",
	              "$remote_path:refs/remotes/$pfx*" .
	                ('/*' x (($remote_path =~ tr/*/*/) - 1)) );
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
		my $author;
		my $saw_from = 0;
		my $msgbuf = "";
		while (<$msg_fh>) {
			if (!$in_msg) {
				$in_msg = 1 if (/^\s*$/);
				$author = $1 if (/^author (.*>)/);
			} elsif (/^git-svn-id: /) {
				# skip this for now, we regenerate the
				# correct one on re-fetch anyways
				# TODO: set *:merge properties or like...
			} else {
				if (/^From:/ || /^Signed-off-by:/) {
					$saw_from = 1;
				}
				$msgbuf .= $_;
			}
		}
		$msgbuf =~ s/\s+$//s;
		if ($Git::SVN::_add_author_from && defined($author)
		    && !$saw_from) {
			$msgbuf .= "\n\nFrom: $author";
		}
		print $log_fh $msgbuf or croak $!;
		command_close_pipe($msg_fh, $ctx);
	}
	close $log_fh or croak $!;

	if ($_edit || ($type eq 'tree')) {
		chomp(my $editor = command_oneline(qw(var GIT_EDITOR)));
		system('sh', '-c', $editor.' "$@"', $editor, $commit_editmsg);
	}
	rename $commit_editmsg, $commit_msg or croak $!;
	{
		require Encode;
		# SVN requires messages to be UTF-8 when entering the repo
		local $/;
		open $log_fh, '<', $commit_msg or croak $!;
		binmode $log_fh;
		chomp($log_entry{log} = <$log_fh>);

		my $enc = Git::config('i18n.commitencoding') || 'UTF-8';
		my $msg = $log_entry{log};

		eval { $msg = Encode::decode($enc, $msg, 1) };
		if ($@) {
			die "Could not decode as $enc:\n", $msg,
			    "\nPerhaps you need to set i18n.commitencoding\n";
		}

		eval { $msg = Encode::encode('UTF-8', $msg, 1) };
		die "Could not encode as UTF-8:\n$msg\n" if $@;

		$log_entry{log} = $msg;

		close $log_fh or croak $!;
	}
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
		next unless /^(.+?|\(no author\))\s*=\s*(.+?)\s*<(.+)>\s*$/;
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
sub read_git_config {
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
		my $arg = 'git config';
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
							\s([a-f\d\-]+)$/ix);
	if (!defined $rev || !$uuid || !$url) {
		# some of the original repositories I made had
		# identifiers like this:
		($rev, $uuid) = ($id =~/^\s*git-svn-id:\s(\d+)\@([a-f\d\-]+)/i);
	}
	return ($url, $rev, $uuid);
}

sub cmt_metadata {
	return extract_metadata((grep(/^git-svn-id: /,
		command(qw/cat-file commit/, shift)))[-1]);
}

sub cmt_sha2rev_batch {
	my %s2r;
	my ($pid, $in, $out, $ctx) = command_bidi_pipe(qw/cat-file --batch/);
	my $list = shift;

	foreach my $sha (@{$list}) {
		my $first = 1;
		my $size = 0;
		print $out $sha, "\n";

		while (my $line = <$in>) {
			if ($first && $line =~ /^[[:xdigit:]]{40}\smissing$/) {
				last;
			} elsif ($first &&
			       $line =~ /^[[:xdigit:]]{40}\scommit\s(\d+)$/) {
				$first = 0;
				$size = $1;
				next;
			} elsif ($line =~ /^(git-svn-id: )/) {
				my (undef, $rev, undef) =
				                      extract_metadata($line);
				$s2r{$sha} = $rev;
			}

			$size -= length($line);
			last if ($size == 0);
		}
	}

	command_close_bidi_pipe($pid, $in, $out, $ctx);

	return \%s2r;
}

sub working_head_info {
	my ($head, $refs) = @_;
	my @args = qw/log --no-color --no-decorate --first-parent
	              --pretty=medium/;
	my ($fh, $ctx) = command_output_pipe(@args, $head);
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
				my $c = $gs->rev_map_get($rev, $uuid);
				if ($c && $c eq $hash) {
					close $fh; # break the pipe
					return ($url, $rev, $uuid, $gs);
				} else {
					$max{$url} ||= $gs->rev_map_max;
				}
			}
		}
	}
	command_close_pipe($fh, $ctx);
	(undef, undef, undef, undef);
}

sub read_commit_parents {
	my ($parents, $c) = @_;
	chomp(my $p = command_oneline(qw/rev-list --parents -1/, $c));
	$p =~ s/^($c)\s*// or die "rev-list --parents -1 $c failed!\n";
	@{$parents->{$c}} = split(/ /, $p);
}

sub linearize_history {
	my ($gs, $refs) = @_;
	my %parents;
	foreach my $c (@$refs) {
		read_commit_parents(\%parents, $c);
	}

	my @linear_refs;
	my %skip = ();
	my $last_svn_commit = $gs->last_commit;
	foreach my $c (reverse @$refs) {
		next if $c eq $last_svn_commit;
		last if $skip{$c};

		unshift @linear_refs, $c;
		$skip{$c} = 1;

		# we only want the first parent to diff against for linear
		# history, we save the rest to inject when we finalize the
		# svn commit
		my $fp_a = verify_ref("$c~1");
		my $fp_b = shift @{$parents{$c}} if $parents{$c};
		if (!$fp_a || !$fp_b) {
			die "Commit $c\n",
			    "has no parent commit, and therefore ",
			    "nothing to diff against.\n",
			    "You should be working from a repository ",
			    "originally created by git-svn\n";
		}
		if ($fp_a ne $fp_b) {
			die "$c~1 = $fp_a, however parsing commit $c ",
			    "revealed that:\n$c~1 = $fp_b\nBUG!\n";
		}

		foreach my $p (@{$parents{$c}}) {
			$skip{$p} = 1;
		}
	}
	(\@linear_refs, \%parents);
}

sub find_file_type_and_diff_status {
	my ($path) = @_;
	return ('dir', '') if $path eq '';

	my $diff_output =
	    command_oneline(qw(diff --cached --name-status --), $path) || "";
	my $diff_status = (split(' ', $diff_output))[0] || "";

	my $ls_tree = command_oneline(qw(ls-tree HEAD), $path) || "";

	return (undef, undef) if !$diff_status && !$ls_tree;

	if ($diff_status eq "A") {
		return ("link", $diff_status) if -l $path;
		return ("dir", $diff_status) if -d $path;
		return ("file", $diff_status);
	}

	my $mode = (split(' ', $ls_tree))[0] || "";

	return ("link", $diff_status) if $mode eq "120000";
	return ("dir", $diff_status) if $mode eq "040000";
	return ("file", $diff_status);
}

sub md5sum {
	my $arg = shift;
	my $ref = ref $arg;
	my $md5 = Digest::MD5->new();
        if ($ref eq 'GLOB' || $ref eq 'IO::File' || $ref eq 'File::Temp') {
		$md5->addfile($arg) or croak $!;
	} elsif ($ref eq 'SCALAR') {
		$md5->add($$arg) or croak $!;
	} elsif (!$ref) {
		$md5->add($arg) or croak $!;
	} else {
		::fatal "Can't provide MD5 hash for unknown ref type: '", $ref, "'";
	}
	return $md5->hexdigest();
}

sub gc_directory {
	if ($can_compress && -f $_ && basename($_) eq "unhandled.log") {
		my $out_filename = $_ . ".gz";
		open my $in_fh, "<", $_ or die "Unable to open $_: $!\n";
		binmode $in_fh;
		my $gz = Compress::Zlib::gzopen($out_filename, "ab") or
				die "Unable to open $out_filename: $!\n";

		my $res;
		while ($res = sysread($in_fh, my $str, 1024)) {
			$gz->gzwrite($str) or
				die "Unable to write: ".$gz->gzerror()."!\n";
		}
		unlink $_ or die "unlink $File::Find::name: $!\n";
	} elsif (-f $_ && basename($_) eq "index") {
		unlink $_ or die "unlink $_: $!\n";
	}
}

package Git::SVN;
use strict;
use warnings;
use Fcntl qw/:DEFAULT :seek/;
use constant rev_map_fmt => 'NH40';
use vars qw/$default_repo_id $default_ref_id $_no_metadata $_follow_parent
            $_repack $_repack_flags $_use_svm_props $_head
            $_use_svnsync_props $no_reuse_existing $_minimize_url
	    $_use_log_author $_add_author_from $_localtime/;
use Carp qw/croak/;
use File::Path qw/mkpath/;
use File::Copy qw/copy/;
use IPC::Open3;
use Memoize;  # core since 5.8.0, Jul 2002
use Memoize::Storable;

my ($_gc_nr, $_gc_period);

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


my (%LOCKFILES, %INDEX_FILES);
END {
	unlink keys %LOCKFILES if %LOCKFILES;
	unlink keys %INDEX_FILES if %INDEX_FILES;
}

sub resolve_local_globs {
	my ($url, $fetch, $glob_spec) = @_;
	return unless defined $glob_spec;
	my $ref = $glob_spec->{ref};
	my $path = $glob_spec->{path};
	foreach (command(qw#for-each-ref --format=%(refname) refs/#)) {
		next unless m#^$ref->{regex}$#;
		my $p = $1;
		my $pathname = desanitize_refname($path->full_path($p));
		my $refname = desanitize_refname($ref->full_path($p));
		if (my $existing = $fetch->{$pathname}) {
			if ($existing ne $refname) {
				die "Refspec conflict:\n",
				    "existing: $existing\n",
				    " globbed: $refname\n";
			}
			my $u = (::cmt_metadata("$refname"))[0];
			$u =~ s!^\Q$url\E(/|$)!! or die
			  "$refname: '$url' not found in '$u'\n";
			if ($pathname ne $u) {
				warn "W: Refspec glob conflict ",
				     "(ref: $refname):\n",
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

	# ignore errors, $head revision may not even exist anymore
	eval { $ra->get_log("", $head, 0, 1, 0, 1, sub { $head = $_[1] }) };
	warn "W: $@\n" if $@;

	my $base = defined $fetch ? $head : 0;

	# read the max revs for wildcard expansion (branches/*, tags/*)
	foreach my $t (qw/branches tags/) {
		defined $remote->{$t} or next;
		push @globs, @{$remote->{$t}};

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
			my $lr = $gs->rev_map_max;
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
	my $use_svm_props = eval { command_oneline(qw/config --bool
	    svn.useSvmProps/) };
	$use_svm_props = $use_svm_props eq 'true' if $use_svm_props;
	my $svn_refspec = qr{\s*(.*?)\s*:\s*(.+?)\s*};
	foreach (grep { s/^svn-remote\.// } command(qw/config -l/)) {
		if (m!^(.+)\.fetch=$svn_refspec$!) {
			my ($remote, $local_ref, $remote_ref) = ($1, $2, $3);
			die("svn-remote.$remote: remote ref '$remote_ref' "
			    . "must start with 'refs/'\n")
				unless $remote_ref =~ m{^refs/};
			$local_ref = uri_decode($local_ref);
			$r->{$remote}->{fetch}->{$local_ref} = $remote_ref;
			$r->{$remote}->{svm} = {} if $use_svm_props;
		} elsif (m!^(.+)\.usesvmprops=\s*(.*)\s*$!) {
			$r->{$1}->{svm} = {};
		} elsif (m!^(.+)\.url=\s*(.*)\s*$!) {
			$r->{$1}->{url} = $2;
		} elsif (m!^(.+)\.pushurl=\s*(.*)\s*$!) {
			$r->{$1}->{pushurl} = $2;
		} elsif (m!^(.+)\.ignore-refs=\s*(.*)\s*$!) {
			$r->{$1}->{ignore_refs_regex} = $2;
		} elsif (m!^(.+)\.(branches|tags)=$svn_refspec$!) {
			my ($remote, $t, $local_ref, $remote_ref) =
			                                     ($1, $2, $3, $4);
			die("svn-remote.$remote: remote ref '$remote_ref' ($t) "
			    . "must start with 'refs/'\n")
				unless $remote_ref =~ m{^refs/};
			$local_ref = uri_decode($local_ref);
			my $rs = {
			    t => $t,
			    remote => $remote,
			    path => Git::SVN::GlobSpec->new($local_ref, 1),
			    ref => Git::SVN::GlobSpec->new($remote_ref, 0) };
			if (length($rs->{ref}->{right}) != 0) {
				die "The '*' glob character must be the last ",
				    "character of '$remote_ref'\n";
			}
			push @{ $r->{$remote}->{$t} }, $rs;
		}
	}

	map {
		if (defined $r->{$_}->{svm}) {
			my $svm;
			eval {
				my $section = "svn-remote.$_";
				$svm = {
					source => tmp_config('--get',
					    "$section.svm-source"),
					replace => tmp_config('--get',
					    "$section.svm-replace"),
				}
			};
			$r->{$_}->{svm} = $svm;
		}
	} keys %$r;

	foreach my $remote (keys %$r) {
		foreach ( grep { defined $_ }
			  map { $r->{$remote}->{$_} } qw(branches tags) ) {
			foreach my $rs ( @$_ ) {
				$rs->{ignore_refs_regex} =
				    $r->{$remote}->{ignore_refs_regex};
			}
		}
	}

	$r;
}

sub init_vars {
	$_gc_nr = $_gc_period = 1000;
	if (defined $_repack || defined $_repack_flags) {
	       warn "Repack options are obsolete; they have no effect.\n";
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
	if (!$no_write && defined $xpath) {
		die "svn-remote.$xrepo_id.fetch already set to track ",
		    "$xpath:", $self->refname, "\n";
	}
	unless ($no_write) {
		command_noisy('config',
			      "svn-remote.$self->{repo_id}.url", $url);
		$self->{path} =~ s{^/}{};
		$self->{path} =~ s{%([0-9A-F]{2})}{chr hex($1)}ieg;
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
		foreach my $t (qw/branches tags/) {
			foreach my $globspec (@{$remotes->{$repo_id}->{$t}}) {
				resolve_local_globs($u, $fetch, $globspec);
			}
		}
		my $p = $path;
		my $rwr = rewrite_root({repo_id => $repo_id});
		my $svm = $remotes->{$repo_id}->{svm}
			if defined $remotes->{$repo_id}->{svm};
		unless (defined $p) {
			$p = $full_url;
			my $z = $u;
			my $prefix = '';
			if ($rwr) {
				$z = $rwr;
				remove_username($z);
			} elsif (defined $svm) {
				$z = $svm->{source};
				$prefix = $svm->{replace};
				$prefix =~ s#^\Q$u\E(?:/|$)##;
				$prefix =~ s#/$##;
			}
			$p =~ s#^\Q$z\E(?:/|$)#$prefix# or next;
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
		              \s*(.*?)\s*:\s*(.+?)\s*$!x;
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
			    "$ref_id\n";
		}
	}
	my $self = _new($class, $repo_id, $ref_id, $path);
	if (!defined $self->{path} || !length $self->{path}) {
		my $fetch = command_oneline('config', '--get',
		                            "svn-remote.$repo_id.fetch",
		                            ":$ref_id\$") or
		     die "Failed to read \"svn-remote.$repo_id.fetch\" ",
		         "\":$ref_id\$\" in config\n";
		($self->{path}, undef) = split(/\s*:\s*/, $fetch);
	}
	$self->{path} =~ s{/+}{/}g;
	$self->{path} =~ s{\A/}{};
	$self->{path} =~ s{/\z}{};
	$self->{url} = command_oneline('config', '--get',
	                               "svn-remote.$repo_id.url") or
                  die "Failed to read \"svn-remote.$repo_id.url\" in config\n";
	$self->{pushurl} = eval { command_oneline('config', '--get',
	                          "svn-remote.$repo_id.pushurl") };
	$self->rebuild;
	$self;
}

sub refname {
	my ($refname) = $_[0]->{ref_id} ;

	# It cannot end with a slash /, we'll throw up on this because
	# SVN can't have directories with a slash in their name, either:
	if ($refname =~ m{/$}) {
		die "ref: '$refname' ends with a trailing slash, this is ",
		    "not permitted by git nor Subversion\n";
	}

	# It cannot have ASCII control character space, tilde ~, caret ^,
	# colon :, question-mark ?, asterisk *, space, or open bracket [
	# anywhere.
	#
	# Additionally, % must be escaped because it is used for escaping
	# and we want our escaped refname to be reversible
	$refname =~ s{([ \%~\^:\?\*\[\t])}{uc sprintf('%%%02x',ord($1))}eg;

	# no slash-separated component can begin with a dot .
	# /.* becomes /%2E*
	$refname =~ s{/\.}{/%2E}g;

	# It cannot have two consecutive dots .. anywhere
	# .. becomes %2E%2E
	$refname =~ s{\.\.}{%2E%2E}g;

	# trailing dots and .lock are not allowed
	# .$ becomes %2E and .lock becomes %2Elock
	$refname =~ s{\.(?=$|lock$)}{%2E};

	# the sequence @{ is used to access the reflog
	# @{ becomes %40{
	$refname =~ s{\@\{}{%40\{}g;

	return $refname;
}

sub desanitize_refname {
	my ($refname) = @_;
	$refname =~ s{%(?:([0-9A-F]{2}))}{chr hex($1)}eg;
	return $refname;
}

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

		$uuid =~ m{^[0-9a-f\-]{30,}$}i
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
	if ($self->rewrite_uuid) {
		die "Can't have both 'useSvnsyncProps' and 'rewriteUUID' ",
		    "options set!\n";
	}

	my $svnsync;
	# see if we have it in our config, first:
	eval {
		my $section = "svn-remote.$self->{repo_id}";

		my $url = tmp_config('--get', "$section.svnsync-url");
		($url) = ($url =~ m{^([a-z\+]+://\S+)$}) or
		   die "doesn't look right - svn:sync-from-url is '$url'\n";

		my $uuid = tmp_config('--get', "$section.svnsync-uuid");
		($uuid) = ($uuid =~ m{^([0-9a-f\-]{30,})$}i) or
		   die "doesn't look right - svn:sync-from-uuid is '$uuid'\n";

		$svnsync = { url => $url, uuid => $uuid }
	};
	if ($svnsync && $svnsync->{url} && $svnsync->{uuid}) {
		return $self->{svnsync} = $svnsync;
	}

	my $err = "useSvnsyncProps set, but failed to read " .
	          "svnsync property: svn:sync-from-";
	my $rp = $self->ra->rev_proplist(0);

	my $url = $rp->{'svn:sync-from-url'} or die $err . "url\n";
	($url) = ($url =~ m{^([a-z\+]+://\S+)$}) or
	           die "doesn't look right - svn:sync-from-url is '$url'\n";

	my $uuid = $rp->{'svn:sync-from-uuid'} or die $err . "uuid\n";
	($uuid) = ($uuid =~ m{^([0-9a-f\-]{30,})$}i) or
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
		if (!$@ && $uuid && $uuid =~ /^([a-f\d\-]{30,})$/i) {
			$self->{ra_uuid} = $uuid;
		} else {
			die "ra_uuid called without URL\n" unless $self->{url};
			$self->{ra_uuid} = $self->ra->get_uuid;
			tmp_config('--add', $key, $self->{ra_uuid});
		}
	}
	$self->{ra_uuid};
}

sub _set_repos_root {
	my ($self, $repos_root) = @_;
	my $k = "svn-remote.$self->{repo_id}.reposRoot";
	$repos_root ||= $self->ra->{repos_root};
	tmp_config($k, $repos_root);
	$repos_root;
}

sub repos_root {
	my ($self) = @_;
	my $k = "svn-remote.$self->{repo_id}.reposRoot";
	eval { tmp_config('--get', $k) } || $self->_set_repos_root;
}

sub ra {
	my ($self) = shift;
	my $ra = Git::SVN::Ra->new($self->{url});
	$self->_set_repos_root($ra->{repos_root});
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

# prop_walk(PATH, REV, SUB)
# -------------------------
# Recursively traverse PATH at revision REV and invoke SUB for each
# directory that contains a SVN property.  SUB will be invoked as
# follows:  &SUB(gs, path, props);  where `gs' is this instance of
# Git::SVN, `path' the path to the directory where the properties
# `props' were found.  The `path' will be relative to point of checkout,
# that is, if url://repo/trunk is the current Git branch, and that
# directory contains a sub-directory `d', SUB will be invoked with `/d/'
# as `path' (note the trailing `/').
sub prop_walk {
	my ($self, $path, $rev, $sub) = @_;

	$path =~ s#^/##;
	my ($dirent, undef, $props) = $self->ra->get_dir($path, $rev);
	$path =~ s#^/*#/#g;
	my $p = $path;
	# Strip the irrelevant part of the path.
	$p =~ s#^/+\Q$self->{path}\E(/|$)#/#;
	# Ensure the path is terminated by a `/'.
	$p =~ s#/*$#/#;

	# The properties contain all the internal SVN stuff nobody
	# (usually) cares about.
	my $interesting_props = 0;
	foreach (keys %{$props}) {
		# If it doesn't start with `svn:', it must be a
		# user-defined property.
		++$interesting_props and next if $_ !~ /^svn:/;
		# FIXME: Fragile, if SVN adds new public properties,
		# this needs to be updated.
		++$interesting_props if /^svn:(?:ignore|keywords|executable
		                                 |eol-style|mime-type
						 |externals|needs-lock)$/x;
	}
	&$sub($self, $p, $props) if $interesting_props;

	foreach (sort keys %$dirent) {
		next if $dirent->{$_}->{kind} != $SVN::Node::dir;
		$self->prop_walk($self->{path} . $p . $_, $rev, $sub);
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
	my $map_path = $self->map_path;
	unless (-e $map_path) {
		($self->{last_rev}, $self->{last_commit}) = (undef, undef);
		return (undef, undef);
	}
	my ($rev, $commit) = $self->rev_map_max(1);
	($self->{last_rev}, $self->{last_commit}) = ($rev, $commit);
	return ($rev, $commit);
}

sub get_fetch_range {
	my ($self, $min, $max) = @_;
	$max ||= $self->ra->get_latest_revnum;
	$min ||= $self->rev_map_max;
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
			        "Something is seriously wrong...";
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
	if (my $ipd = $self->{inject_parents_dcommit}) {
		if (my $commit = delete $ipd->{$log_entry->{revision}}) {
			push @tmp, @$commit;
		}
	}
	push @tmp, $_ foreach (@{$log_entry->{parents}}, @tmp);
	while (my $p = shift @tmp) {
		next if $seen{$p};
		$seen{$p} = 1;
		push @ret, $p;
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

sub rewrite_uuid {
	my ($self) = @_;
	return $self->{-rewrite_uuid} if exists $self->{-rewrite_uuid};
	my $k = "svn-remote.$self->{repo_id}.rewriteUUID";
	my $rwid = eval { command_oneline(qw/config --get/, $k) };
	if ($rwid) {
		$rwid =~ s#/+$##;
		if ($rwid !~ m#^[a-f0-9]{8}-(?:[a-f0-9]{4}-){3}[a-f0-9]{12}$#) {
			die "$rwid is not a valid UUID (key: $k)\n";
		}
	}
	$self->{-rewrite_uuid} = $rwid;
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

sub full_pushurl {
	my ($self) = @_;
	if ($self->{pushurl}) {
		return $self->{pushurl} . (length $self->{path} ? '/' .
		       $self->{path} : '');
	} else {
		return $self->full_url;
	}
}

sub set_commit_header_env {
	my ($log_entry) = @_;
	my %env;
	foreach my $ned (qw/NAME EMAIL DATE/) {
		foreach my $ac (qw/AUTHOR COMMITTER/) {
			$env{"GIT_${ac}_${ned}"} = $ENV{"GIT_${ac}_${ned}"};
		}
	}

	$ENV{GIT_AUTHOR_NAME} = $log_entry->{name};
	$ENV{GIT_AUTHOR_EMAIL} = $log_entry->{email};
	$ENV{GIT_AUTHOR_DATE} = $ENV{GIT_COMMITTER_DATE} = $log_entry->{date};

	$ENV{GIT_COMMITTER_NAME} = (defined $log_entry->{commit_name})
						? $log_entry->{commit_name}
						: $log_entry->{name};
	$ENV{GIT_COMMITTER_EMAIL} = (defined $log_entry->{commit_email})
						? $log_entry->{commit_email}
						: $log_entry->{email};
	\%env;
}

sub restore_commit_header_env {
	my ($env) = @_;
	foreach my $ned (qw/NAME EMAIL DATE/) {
		foreach my $ac (qw/AUTHOR COMMITTER/) {
			my $k = "GIT_${ac}_${ned}";
			if (defined $env->{$k}) {
				$ENV{$k} = $env->{$k};
			} else {
				delete $ENV{$k};
			}
		}
	}
}

sub gc {
	command_noisy('gc', '--auto');
};

sub do_git_commit {
	my ($self, $log_entry) = @_;
	my $lr = $self->last_rev;
	if (defined $lr && $lr >= $log_entry->{revision}) {
		die "Last fetched revision of ", $self->refname,
		    " was r$lr, but we are about to fetch: ",
		    "r$log_entry->{revision}!\n";
	}
	if (my $c = $self->rev_map_get($log_entry->{revision})) {
		croak "$log_entry->{revision} = $c already exists! ",
		      "Why are we refetching it?\n";
	}
	my $old_env = set_commit_header_env($log_entry);
	my $tree = $log_entry->{tree};
	if (!defined $tree) {
		$tree = $self->tmp_index_do(sub {
		                            command_oneline('write-tree') });
	}
	die "Tree is not a valid sha1: $tree\n" if $tree !~ /^$::sha1$/o;

	my @exec = ('git', 'commit-tree', $tree);
	foreach ($self->get_commit_parents($log_entry)) {
		push @exec, '-p', $_;
	}
	defined(my $pid = open3(my $msg_fh, my $out_fh, '>&STDERR', @exec))
	                                                           or croak $!;
	binmode $msg_fh;

	# we always get UTF-8 from SVN, but we may want our commits in
	# a different encoding.
	if (my $enc = Git::config('i18n.commitencoding')) {
		require Encode;
		Encode::from_to($log_entry->{log}, 'UTF-8', $enc);
	}
	print $msg_fh $log_entry->{log} or croak $!;
	restore_commit_header_env($old_env);
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

	$self->rev_map_set($log_entry->{revision}, $commit, 1);

	$self->{last_rev} = $log_entry->{revision};
	$self->{last_commit} = $commit;
	print "r$log_entry->{revision}" unless $::_q > 1;
	if (defined $log_entry->{svm_revision}) {
		 print " (\@$log_entry->{svm_revision})" unless $::_q > 1;
		 $self->rev_map_set($log_entry->{svm_revision}, $commit,
		                   0, $self->svm_uuid);
	}
	print " = $commit ($self->{ref_id})\n" unless $::_q > 1;
	if (--$_gc_nr == 0) {
		$_gc_nr = $_gc_period;
		gc();
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
		$self->ra->get_log([$self->{path}], $rev, $rev, 0, 1, 1,
				   sub { $paths = $_[0] });
		$SVN::Error::handler = $err_handler;
	}
	return undef unless defined $paths;

	# look for a parent from another branch:
	my @b_path_components = split m#/#, $self->{path};
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
	my $new_url = $url . $branch_from;
	print STDERR  "Found possible branch point: ",
	              "$new_url => ", $self->full_url, ", $r\n"
	              unless $::_q > 1;
	$branch_from =~ s#^/##;
	my $gs = $self->other_gs($new_url, $url,
		                 $branch_from, $r, $self->{ref_id});
	my ($r0, $parent) = $gs->find_rev_before($r, 1);
	{
		my ($base, $head);
		if (!defined $r0 || !defined $parent) {
			($base, $head) = parse_revision_argument(0, $r);
		} else {
			if ($r0 < $r) {
				$gs->ra->get_log([$gs->{path}], $r0 + 1, $r, 1,
					0, 1, sub { $base = $_[1] - 1 });
			}
		}
		if (defined $base && $base <= $r) {
			$gs->fetch($base, $r);
		}
		($r0, $parent) = $gs->find_rev_before($r, 1);
	}
	if (defined $r0 && defined $parent) {
		print STDERR "Found branch parent: ($self->{ref_id}) $parent\n"
		             unless $::_q > 1;
		my $ed;
		if ($self->ra->can_do_switch) {
			$self->assert_index_clean($parent);
			print STDERR "Following parent with do_switch\n"
			             unless $::_q > 1;
			# do_switch works with svn/trunk >= r22312, but that
			# is not included with SVN 1.4.3 (the latest version
			# at the moment), so we can't rely on it
			$self->{last_rev} = $r0;
			$self->{last_commit} = $parent;
			$ed = SVN::Git::Fetcher->new($self, $gs->{path});
			$gs->ra->gs_do_switch($r0, $rev, $gs,
					      $self->full_url, $ed)
			  or die "SVN connection failed somewhere...\n";
		} elsif ($self->ra->trees_match($new_url, $r0,
			                        $self->full_url, $rev)) {
			print STDERR "Trees match:\n",
			             "  $new_url\@$r0\n",
			             "  ${\$self->full_url}\@$rev\n",
			             "Following parent with no changes\n"
			             unless $::_q > 1;
			$self->tmp_index_do(sub {
			    command_noisy('read-tree', $parent);
			});
			$self->{last_commit} = $parent;
		} else {
			print STDERR "Following parent with do_update\n"
			             unless $::_q > 1;
			$ed = SVN::Git::Fetcher->new($self);
			$self->ra->gs_do_update($rev, $rev, $self, $ed)
			  or die "SVN connection failed somewhere...\n";
		}
		print STDERR "Successfully followed parent\n" unless $::_q > 1;
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

sub mkemptydirs {
	my ($self, $r) = @_;

	sub scan {
		my ($r, $empty_dirs, $line) = @_;
		if (defined $r && $line =~ /^r(\d+)$/) {
			return 0 if $1 > $r;
		} elsif ($line =~ /^  \+empty_dir: (.+)$/) {
			$empty_dirs->{$1} = 1;
		} elsif ($line =~ /^  \-empty_dir: (.+)$/) {
			my @d = grep {m[^\Q$1\E(/|$)]} (keys %$empty_dirs);
			delete @$empty_dirs{@d};
		}
		1; # continue
	};

	my %empty_dirs = ();
	my $gz_file = "$self->{dir}/unhandled.log.gz";
	if (-f $gz_file) {
		if (!$can_compress) {
			warn "Compress::Zlib could not be found; ",
			     "empty directories in $gz_file will not be read\n";
		} else {
			my $gz = Compress::Zlib::gzopen($gz_file, "rb") or
				die "Unable to open $gz_file: $!\n";
			my $line;
			while ($gz->gzreadline($line) > 0) {
				scan($r, \%empty_dirs, $line) or last;
			}
			$gz->gzclose;
		}
	}

	if (open my $fh, '<', "$self->{dir}/unhandled.log") {
		binmode $fh or croak "binmode: $!";
		while (<$fh>) {
			scan($r, \%empty_dirs, $_) or last;
		}
		close $fh;
	}

	my $strip = qr/\A\Q$self->{path}\E(?:\/|$)/;
	foreach my $d (sort keys %empty_dirs) {
		$d = uri_decode($d);
		$d =~ s/$strip//;
		next unless length($d);
		next if -d $d;
		if (-e $d) {
			warn "$d exists but is not a directory\n";
		} else {
			print "creating empty directory: $d\n";
			mkpath([$d]);
		}
	}
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

# parse_svn_date(DATE)
# --------------------
# Given a date (in UTC) from Subversion, return a string in the format
# "<TZ Offset> <local date/time>" that Git will use.
#
# By default the parsed date will be in UTC; if $Git::SVN::_localtime
# is true we'll convert it to the local timezone instead.
sub parse_svn_date {
	my $date = shift || return '+0000 1970-01-01 00:00:00';
	my ($Y,$m,$d,$H,$M,$S) = ($date =~ /^(\d{4})\-(\d\d)\-(\d\d)T
	                                    (\d\d)\:(\d\d)\:(\d\d)\.\d*Z$/x) or
	                                 croak "Unable to parse date: $date\n";
	my $parsed_date;    # Set next.

	if ($Git::SVN::_localtime) {
		# Translate the Subversion datetime to an epoch time.
		# Begin by switching ourselves to $date's timezone, UTC.
		my $old_env_TZ = $ENV{TZ};
		$ENV{TZ} = 'UTC';

		my $epoch_in_UTC =
		    POSIX::strftime('%s', $S, $M, $H, $d, $m - 1, $Y - 1900);

		# Determine our local timezone (including DST) at the
		# time of $epoch_in_UTC.  $Git::SVN::Log::TZ stored the
		# value of TZ, if any, at the time we were run.
		if (defined $Git::SVN::Log::TZ) {
			$ENV{TZ} = $Git::SVN::Log::TZ;
		} else {
			delete $ENV{TZ};
		}

		my $our_TZ =
		    POSIX::strftime('%Z', $S, $M, $H, $d, $m - 1, $Y - 1900);

		# This converts $epoch_in_UTC into our local timezone.
		my ($sec, $min, $hour, $mday, $mon, $year,
		    $wday, $yday, $isdst) = localtime($epoch_in_UTC);

		$parsed_date = sprintf('%s %04d-%02d-%02d %02d:%02d:%02d',
				       $our_TZ, $year + 1900, $mon + 1,
				       $mday, $hour, $min, $sec);

		# Reset us to the timezone in effect when we entered
		# this routine.
		if (defined $old_env_TZ) {
			$ENV{TZ} = $old_env_TZ;
		} else {
			delete $ENV{TZ};
		}
	} else {
		$parsed_date = "+0000 $Y-$m-$d $H:$M:$S";
	}

	return $parsed_date;
}

sub other_gs {
	my ($self, $new_url, $url,
	    $branch_from, $r, $old_ref_id) = @_;
	my $gs = Git::SVN->find_by_url($new_url, $url, $branch_from);
	unless ($gs) {
		my $ref_id = $old_ref_id;
		$ref_id =~ s/\@\d+-*$//;
		$ref_id .= "\@$r";
		# just grow a tail if we're not unique enough :x
		$ref_id .= '-' while find_ref($ref_id);
		my ($u, $p, $repo_id) = ($new_url, '', $ref_id);
		if ($u =~ s#^\Q$url\E(/|$)##) {
			$p = $u;
			$u = $url;
			$repo_id = $self->{repo_id};
		}
		while (1) {
			# It is possible to tag two different subdirectories at
			# the same revision.  If the url for an existing ref
			# does not match, we must either find a ref with a
			# matching url or create a new ref by growing a tail.
			$gs = Git::SVN->init($u, $p, $repo_id, $ref_id, 1);
			my (undef, $max_commit) = $gs->rev_map_max(1);
			last if (!$max_commit);
			my ($url) = ::cmt_metadata($max_commit);
			last if ($url eq $gs->metadata_url);
			$ref_id .= '-';
		}
		print STDERR "Initializing parent: $ref_id\n" unless $::_q > 1;
	}
	$gs
}

sub call_authors_prog {
	my ($orig_author) = @_;
	$orig_author = command_oneline('rev-parse', '--sq-quote', $orig_author);
	my $author = `$::_authors_prog $orig_author`;
	if ($? != 0) {
		die "$::_authors_prog failed with exit code $?\n"
	}
	if ($author =~ /^\s*(.+?)\s*<(.*)>\s*$/) {
		my ($name, $email) = ($1, $2);
		$email = undef if length $2 == 0;
		return [$name, $email];
	} else {
		die "Author: $orig_author: $::_authors_prog returned "
			. "invalid author format: $author\n";
	}
}

sub check_author {
	my ($author) = @_;
	if (!defined $author || length $author == 0) {
		$author = '(no author)';
	}
	if (!defined $::users{$author}) {
		if (defined $::_authors_prog) {
			$::users{$author} = call_authors_prog($author);
		} elsif (defined $::_authors) {
			die "Author: $author not defined in $::_authors file\n";
		}
	}
	$author;
}

sub find_extra_svk_parents {
	my ($self, $ed, $tickets, $parents) = @_;
	# aha!  svk:merge property changed...
	my @tickets = split "\n", $tickets;
	my @known_parents;
	for my $ticket ( @tickets ) {
		my ($uuid, $path, $rev) = split /:/, $ticket;
		if ( $uuid eq $self->ra_uuid ) {
			my $url = $self->{url};
			my $repos_root = $url;
			my $branch_from = $path;
			$branch_from =~ s{^/}{};
			my $gs = $self->other_gs($repos_root."/".$branch_from,
			                         $url,
			                         $branch_from,
			                         $rev,
			                         $self->{ref_id});
			if ( my $commit = $gs->rev_map_get($rev, $uuid) ) {
				# wahey!  we found it, but it might be
				# an old one (!)
				push @known_parents, [ $rev, $commit ];
			}
		}
	}
	# Ordering matters; highest-numbered commit merge tickets
	# first, as they may account for later merge ticket additions
	# or changes.
	@known_parents = map {$_->[1]} sort {$b->[0] <=> $a->[0]} @known_parents;
	for my $parent ( @known_parents ) {
		my @cmd = ('rev-list', $parent, map { "^$_" } @$parents );
		my ($msg_fh, $ctx) = command_output_pipe(@cmd);
		my $new;
		while ( <$msg_fh> ) {
			$new=1;last;
		}
		command_close_pipe($msg_fh, $ctx);
		if ( $new ) {
			print STDERR
			    "Found merge parent (svk:merge ticket): $parent\n";
			push @$parents, $parent;
		}
	}
}

sub lookup_svn_merge {
	my $uuid = shift;
	my $url = shift;
	my $merge = shift;

	my ($source, $revs) = split ":", $merge;
	my $path = $source;
	$path =~ s{^/}{};
	my $gs = Git::SVN->find_by_url($url.$source, $url, $path);
	if ( !$gs ) {
		warn "Couldn't find revmap for $url$source\n";
		return;
	}
	my @ranges = split ",", $revs;
	my ($tip, $tip_commit);
	my @merged_commit_ranges;
	# find the tip
	for my $range ( @ranges ) {
		my ($bottom, $top) = split "-", $range;
		$top ||= $bottom;
		my $bottom_commit = $gs->find_rev_after( $bottom, 1, $top );
		my $top_commit = $gs->find_rev_before( $top, 1, $bottom );

		unless ($top_commit and $bottom_commit) {
			warn "W:unknown path/rev in svn:mergeinfo "
				."dirprop: $source:$range\n";
			next;
		}

		if (scalar(command('rev-parse', "$bottom_commit^@"))) {
			push @merged_commit_ranges,
			     "$bottom_commit^..$top_commit";
		} else {
			push @merged_commit_ranges, "$top_commit";
		}

		if ( !defined $tip or $top > $tip ) {
			$tip = $top;
			$tip_commit = $top_commit;
		}
	}
	return ($tip_commit, @merged_commit_ranges);
}

sub _rev_list {
	my ($msg_fh, $ctx) = command_output_pipe(
		"rev-list", @_,
	       );
	my @rv;
	while ( <$msg_fh> ) {
		chomp;
		push @rv, $_;
	}
	command_close_pipe($msg_fh, $ctx);
	@rv;
}

sub check_cherry_pick {
	my $base = shift;
	my $tip = shift;
	my $parents = shift;
	my @ranges = @_;
	my %commits = map { $_ => 1 }
		_rev_list("--no-merges", $tip, "--not", $base, @$parents, "--");
	for my $range ( @ranges ) {
		delete @commits{_rev_list($range, "--")};
	}
	for my $commit (keys %commits) {
		if (has_no_changes($commit)) {
			delete $commits{$commit};
		}
	}
	return (keys %commits);
}

sub has_no_changes {
	my $commit = shift;

	my @revs = split / /, command_oneline(
		qw(rev-list --parents -1 -m), $commit);

	# Commits with no parents, e.g. the start of a partial branch,
	# have changes by definition.
	return 1 if (@revs < 2);

	# Commits with multiple parents, e.g a merge, have no changes
	# by definition.
	return 0 if (@revs > 2);

	return (command_oneline("rev-parse", "$commit^{tree}") eq
		command_oneline("rev-parse", "$commit~1^{tree}"));
}

# The GIT_DIR environment variable is not always set until after the command
# line arguments are processed, so we can't memoize in a BEGIN block.
{
	my $memoized = 0;

	sub memoize_svn_mergeinfo_functions {
		return if $memoized;
		$memoized = 1;

		my $cache_path = "$ENV{GIT_DIR}/svn/.caches/";
		mkpath([$cache_path]) unless -d $cache_path;

		tie my %lookup_svn_merge_cache => 'Memoize::Storable',
		    "$cache_path/lookup_svn_merge.db", 'nstore';
		memoize 'lookup_svn_merge',
			SCALAR_CACHE => 'FAULT',
			LIST_CACHE => ['HASH' => \%lookup_svn_merge_cache],
		;

		tie my %check_cherry_pick_cache => 'Memoize::Storable',
		    "$cache_path/check_cherry_pick.db", 'nstore';
		memoize 'check_cherry_pick',
			SCALAR_CACHE => 'FAULT',
			LIST_CACHE => ['HASH' => \%check_cherry_pick_cache],
		;

		tie my %has_no_changes_cache => 'Memoize::Storable',
		    "$cache_path/has_no_changes.db", 'nstore';
		memoize 'has_no_changes',
			SCALAR_CACHE => ['HASH' => \%has_no_changes_cache],
			LIST_CACHE => 'FAULT',
		;
	}

	sub unmemoize_svn_mergeinfo_functions {
		return if not $memoized;
		$memoized = 0;

		Memoize::unmemoize 'lookup_svn_merge';
		Memoize::unmemoize 'check_cherry_pick';
		Memoize::unmemoize 'has_no_changes';
	}

	Memoize::memoize 'Git::SVN::repos_root';
}

END {
	# Force cache writeout explicitly instead of waiting for
	# global destruction to avoid segfault in Storable:
	# http://rt.cpan.org/Public/Bug/Display.html?id=36087
	unmemoize_svn_mergeinfo_functions();
}

sub parents_exclude {
	my $parents = shift;
	my @commits = @_;
	return unless @commits;

	my @excluded;
	my $excluded;
	do {
		my @cmd = ('rev-list', "-1", @commits, "--not", @$parents );
		$excluded = command_oneline(@cmd);
		if ( $excluded ) {
			my @new;
			my $found;
			for my $commit ( @commits ) {
				if ( $commit eq $excluded ) {
					push @excluded, $commit;
					$found++;
					last;
				}
				else {
					push @new, $commit;
				}
			}
			die "saw commit '$excluded' in rev-list output, "
				."but we didn't ask for that commit (wanted: @commits --not @$parents)"
					unless $found;
			@commits = @new;
		}
	}
		while ($excluded and @commits);

	return @excluded;
}


# note: this function should only be called if the various dirprops
# have actually changed
sub find_extra_svn_parents {
	my ($self, $ed, $mergeinfo, $parents) = @_;
	# aha!  svk:merge property changed...

	memoize_svn_mergeinfo_functions();

	# We first search for merged tips which are not in our
	# history.  Then, we figure out which git revisions are in
	# that tip, but not this revision.  If all of those revisions
	# are now marked as merge, we can add the tip as a parent.
	my @merges = split "\n", $mergeinfo;
	my @merge_tips;
	my $url = $self->{url};
	my $uuid = $self->ra_uuid;
	my %ranges;
	for my $merge ( @merges ) {
		my ($tip_commit, @ranges) =
			lookup_svn_merge( $uuid, $url, $merge );
		unless (!$tip_commit or
				grep { $_ eq $tip_commit } @$parents ) {
			push @merge_tips, $tip_commit;
			$ranges{$tip_commit} = \@ranges;
		} else {
			push @merge_tips, undef;
		}
	}

	my %excluded = map { $_ => 1 }
		parents_exclude($parents, grep { defined } @merge_tips);

	# check merge tips for new parents
	my @new_parents;
	for my $merge_tip ( @merge_tips ) {
		my $spec = shift @merges;
		next unless $merge_tip and $excluded{$merge_tip};

		my $ranges = $ranges{$merge_tip};

		# check out 'new' tips
		my $merge_base;
		eval {
			$merge_base = command_oneline(
				"merge-base",
				@$parents, $merge_tip,
			);
		};
		if ($@) {
			die "An error occurred during merge-base"
				unless $@->isa("Git::Error::Command");

			warn "W: Cannot find common ancestor between ".
			     "@$parents and $merge_tip. Ignoring merge info.\n";
			next;
		}

		# double check that there are no missing non-merge commits
		my (@incomplete) = check_cherry_pick(
			$merge_base, $merge_tip,
			$parents,
			@$ranges,
		       );

		if ( @incomplete ) {
			warn "W:svn cherry-pick ignored ($spec) - missing "
				.@incomplete." commit(s) (eg $incomplete[0])\n";
		} else {
			warn
				"Found merge parent (svn:mergeinfo prop): ",
					$merge_tip, "\n";
			push @new_parents, $merge_tip;
		}
	}

	# cater for merges which merge commits from multiple branches
	if ( @new_parents > 1 ) {
		for ( my $i = 0; $i <= $#new_parents; $i++ ) {
			for ( my $j = 0; $j <= $#new_parents; $j++ ) {
				next if $i == $j;
				next unless $new_parents[$i];
				next unless $new_parents[$j];
				my $revs = command_oneline(
					"rev-list", "-1",
					"$new_parents[$i]..$new_parents[$j]",
				       );
				if ( !$revs ) {
					undef($new_parents[$j]);
				}
			}
		}
	}
	push @$parents, grep { defined } @new_parents;
}

sub make_log_entry {
	my ($self, $rev, $parents, $ed) = @_;
	my $untracked = $self->get_untracked($ed);

	my @parents = @$parents;
	my $ps = $ed->{path_strip} || "";
	for my $path ( grep { m/$ps/ } %{$ed->{dir_prop}} ) {
		my $props = $ed->{dir_prop}{$path};
		if ( $props->{"svk:merge"} ) {
			$self->find_extra_svk_parents
				($ed, $props->{"svk:merge"}, \@parents);
		}
		if ( $props->{"svn:mergeinfo"} ) {
			$self->find_extra_svn_parents
				($ed,
				 $props->{"svn:mergeinfo"},
				 \@parents);
		}
	}

	open my $un, '>>', "$self->{dir}/unhandled.log" or croak $!;
	print $un "r$rev\n" or croak $!;
	print $un $_, "\n" foreach @$untracked;
	my %log_entry = ( parents => \@parents, revision => $rev,
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

	my ($commit_name, $commit_email) = ($name, $email);
	if ($_use_log_author) {
		my $name_field;
		if ($log_entry{log} =~ /From:\s+(.*\S)\s*\n/i) {
			$name_field = $1;
		} elsif ($log_entry{log} =~ /Signed-off-by:\s+(.*\S)\s*\n/i) {
			$name_field = $1;
		}
		if (!defined $name_field) {
			if (!defined $email) {
				$email = $name;
			}
		} elsif ($name_field =~ /(.*?)\s+<(.*)>/) {
			($name, $email) = ($1, $2);
		} elsif ($name_field =~ /(.*)@/) {
			($name, $email) = ($1, $name_field);
		} else {
			($name, $email) = ($name_field, $name_field);
		}
	}
	if (defined $headrev && $self->use_svm_props) {
		if ($self->rewrite_root) {
			die "Can't have both 'useSvmProps' and 'rewriteRoot' ",
			    "options set!\n";
		}
		if ($self->rewrite_uuid) {
			die "Can't have both 'useSvmProps' and 'rewriteUUID' ",
			    "options set!\n";
		}
		my ($uuid, $r) = $headrev =~ m{^([a-f\d\-]{30,}):(\d+)$}i;
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
		$email ||= "$author\@$uuid";
		$commit_email ||= "$author\@$uuid";
	} elsif ($self->use_svnsync_props) {
		my $full_url = $self->svnsync->{url};
		$full_url .= "/$self->{path}" if length $self->{path};
		remove_username($full_url);
		my $uuid = $self->svnsync->{uuid};
		$log_entry{metadata} = "$full_url\@$rev $uuid";
		$email ||= "$author\@$uuid";
		$commit_email ||= "$author\@$uuid";
	} else {
		my $url = $self->metadata_url;
		remove_username($url);
		my $uuid = $self->rewrite_uuid || $self->ra->get_uuid;
		$log_entry{metadata} = "$url\@$rev " . $uuid;
		$email ||= "$author\@" . $uuid;
		$commit_email ||= "$author\@" . $uuid;
	}
	$log_entry{name} = $name;
	$log_entry{email} = $email;
	$log_entry{commit_name} = $commit_name;
	$log_entry{commit_email} = $commit_email;
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
		::fatal("Must have an existing revision to commit");
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

sub rebuild_from_rev_db {
	my ($self, $path) = @_;
	my $r = -1;
	open my $fh, '<', $path or croak "open: $!";
	binmode $fh or croak "binmode: $!";
	while (<$fh>) {
		length($_) == 41 or croak "inconsistent size in ($_) != 41";
		chomp($_);
		++$r;
		next if $_ eq ('0' x 40);
		$self->rev_map_set($r, $_);
		print "r$r = $_\n";
	}
	close $fh or croak "close: $!";
	unlink $path or croak "unlink: $!";
}

sub rebuild {
	my ($self) = @_;
	my $map_path = $self->map_path;
	my $partial = (-e $map_path && ! -z $map_path);
	return unless ::verify_ref($self->refname.'^0');
	if (!$partial && ($self->use_svm_props || $self->no_metadata)) {
		my $rev_db = $self->rev_db_path;
		$self->rebuild_from_rev_db($rev_db);
		if ($self->use_svm_props) {
			my $svm_rev_db = $self->rev_db_path($self->svm_uuid);
			$self->rebuild_from_rev_db($svm_rev_db);
		}
		$self->unlink_rev_db_symlink;
		return;
	}
	print "Rebuilding $map_path ...\n" if (!$partial);
	my ($base_rev, $head) = ($partial ? $self->rev_map_max_norebuild(1) :
		(undef, undef));
	my ($log, $ctx) =
	    command_output_pipe(qw/rev-list --pretty=raw --reverse/,
				($head ? "$head.." : "") . $self->refname,
				'--');
	my $metadata_url = $self->metadata_url;
	remove_username($metadata_url);
	my $svn_uuid = $self->rewrite_uuid || $self->ra_uuid;
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
		if (($uuid ne $svn_uuid) ||
		    ($metadata_url && $url && ($url ne $metadata_url))) {
			next;
		}
		if ($partial && $head) {
			print "Partial-rebuilding $map_path ...\n";
			print "Currently at $base_rev = $head\n";
			$head = undef;
		}

		$self->rev_map_set($rev, $c);
		print "r$rev = $c\n";
	}
	command_close_pipe($log, $ctx);
	print "Done rebuilding $map_path\n" if (!$partial || !$head);
	my $rev_db_path = $self->rev_db_path;
	if (-f $self->rev_db_path) {
		unlink $self->rev_db_path or croak "unlink: $!";
	}
	$self->unlink_rev_db_symlink;
}

# rev_map:
# Tie::File seems to be prone to offset errors if revisions get sparse,
# it's not that fast, either.  Tie::File is also not in Perl 5.6.  So
# one of my favorite modules is out :<  Next up would be one of the DBM
# modules, but I'm not sure which is most portable...
#
# This is the replacement for the rev_db format, which was too big
# and inefficient for large repositories with a lot of sparse history
# (mainly tags)
#
# The format is this:
#   - 24 bytes for every record,
#     * 4 bytes for the integer representing an SVN revision number
#     * 20 bytes representing the sha1 of a git commit
#   - No empty padding records like the old format
#     (except the last record, which can be overwritten)
#   - new records are written append-only since SVN revision numbers
#     increase monotonically
#   - lookups on SVN revision number are done via a binary search
#   - Piping the file to xxd -c24 is a good way of dumping it for
#     viewing or editing (piped back through xxd -r), should the need
#     ever arise.
#   - The last record can be padding revision with an all-zero sha1
#     This is used to optimize fetch performance when using multiple
#     "fetch" directives in .git/config
#
# These files are disposable unless noMetadata or useSvmProps is set

sub _rev_map_set {
	my ($fh, $rev, $commit) = @_;

	binmode $fh or croak "binmode: $!";
	my $size = (stat($fh))[7];
	($size % 24) == 0 or croak "inconsistent size: $size";

	my $wr_offset = 0;
	if ($size > 0) {
		sysseek($fh, -24, SEEK_END) or croak "seek: $!";
		my $read = sysread($fh, my $buf, 24) or croak "read: $!";
		$read == 24 or croak "read only $read bytes (!= 24)";
		my ($last_rev, $last_commit) = unpack(rev_map_fmt, $buf);
		if ($last_commit eq ('0' x40)) {
			if ($size >= 48) {
				sysseek($fh, -48, SEEK_END) or croak "seek: $!";
				$read = sysread($fh, $buf, 24) or
				    croak "read: $!";
				$read == 24 or
				    croak "read only $read bytes (!= 24)";
				($last_rev, $last_commit) =
				    unpack(rev_map_fmt, $buf);
				if ($last_commit eq ('0' x40)) {
					croak "inconsistent .rev_map\n";
				}
			}
			if ($last_rev >= $rev) {
				croak "last_rev is higher!: $last_rev >= $rev";
			}
			$wr_offset = -24;
		}
	}
	sysseek($fh, $wr_offset, SEEK_END) or croak "seek: $!";
	syswrite($fh, pack(rev_map_fmt, $rev, $commit), 24) == 24 or
	  croak "write: $!";
}

sub _rev_map_reset {
	my ($fh, $rev, $commit) = @_;
	my $c = _rev_map_get($fh, $rev);
	$c eq $commit or die "_rev_map_reset(@_) commit $c does not match!\n";
	my $offset = sysseek($fh, 0, SEEK_CUR) or croak "seek: $!";
	truncate $fh, $offset or croak "truncate: $!";
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

sub rev_map_set {
	my ($self, $rev, $commit, $update_ref, $uuid) = @_;
	defined $commit or die "missing arg3\n";
	length $commit == 40 or die "arg3 must be a full SHA1 hexsum\n";
	my $db = $self->map_path($uuid);
	my $db_lock = "$db.lock";
	my $sig;
	$update_ref ||= 0;
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
		copy($db, $db_lock) or die "rev_map_set(@_): ",
					   "Failed to copy: ",
					   "$db => $db_lock ($!)\n";
	} else {
		rename $db, $db_lock or die "rev_map_set(@_): ",
					    "Failed to rename: ",
					    "$db => $db_lock ($!)\n";
	}

	sysopen(my $fh, $db_lock, O_RDWR | O_CREAT)
	     or croak "Couldn't open $db_lock: $!\n";
	$update_ref eq 'reset' ? _rev_map_reset($fh, $rev, $commit) :
				 _rev_map_set($fh, $rev, $commit);
	if ($sync) {
		$fh->flush or die "Couldn't flush $db_lock: $!\n";
		$fh->sync or die "Couldn't sync $db_lock: $!\n";
	}
	close $fh or croak $!;
	if ($update_ref) {
		$_head = $self;
		my $note = "";
		$note = " ($update_ref)" if ($update_ref !~ /^\d*$/);
		command_noisy('update-ref', '-m', "r$rev$note",
		              $self->refname, $commit);
	}
	rename $db_lock, $db or die "rev_map_set(@_): ", "Failed to rename: ",
	                            "$db_lock => $db ($!)\n";
	delete $LOCKFILES{$db_lock};
	if ($update_ref) {
		$SIG{INT} = $SIG{HUP} = $SIG{TERM} = $SIG{ALRM} = $SIG{PIPE} =
		            $SIG{USR1} = $SIG{USR2} = 'DEFAULT';
		kill $sig, $$ if defined $sig;
	}
}

# If want_commit, this will return an array of (rev, commit) where
# commit _must_ be a valid commit in the archive.
# Otherwise, it'll return the max revision (whether or not the
# commit is valid or just a 0x40 placeholder).
sub rev_map_max {
	my ($self, $want_commit) = @_;
	$self->rebuild;
	my ($r, $c) = $self->rev_map_max_norebuild($want_commit);
	$want_commit ? ($r, $c) : $r;
}

sub rev_map_max_norebuild {
	my ($self, $want_commit) = @_;
	my $map_path = $self->map_path;
	stat $map_path or return $want_commit ? (0, undef) : 0;
	sysopen(my $fh, $map_path, O_RDONLY) or croak "open: $!";
	binmode $fh or croak "binmode: $!";
	my $size = (stat($fh))[7];
	($size % 24) == 0 or croak "inconsistent size: $size";

	if ($size == 0) {
		close $fh or croak "close: $!";
		return $want_commit ? (0, undef) : 0;
	}

	sysseek($fh, -24, SEEK_END) or croak "seek: $!";
	sysread($fh, my $buf, 24) == 24 or croak "read: $!";
	my ($r, $c) = unpack(rev_map_fmt, $buf);
	if ($want_commit && $c eq ('0' x40)) {
		if ($size < 48) {
			return $want_commit ? (0, undef) : 0;
		}
		sysseek($fh, -48, SEEK_END) or croak "seek: $!";
		sysread($fh, $buf, 24) == 24 or croak "read: $!";
		($r, $c) = unpack(rev_map_fmt, $buf);
		if ($c eq ('0'x40)) {
			croak "Penultimate record is all-zeroes in $map_path";
		}
	}
	close $fh or croak "close: $!";
	$want_commit ? ($r, $c) : $r;
}

sub rev_map_get {
	my ($self, $rev, $uuid) = @_;
	my $map_path = $self->map_path($uuid);
	return undef unless -e $map_path;

	sysopen(my $fh, $map_path, O_RDONLY) or croak "open: $!";
	my $c = _rev_map_get($fh, $rev);
	close($fh) or croak "close: $!";
	$c
}

sub _rev_map_get {
	my ($fh, $rev) = @_;

	binmode $fh or croak "binmode: $!";
	my $size = (stat($fh))[7];
	($size % 24) == 0 or croak "inconsistent size: $size";

	if ($size == 0) {
		return undef;
	}

	my ($l, $u) = (0, $size - 24);
	my ($r, $c, $buf);

	while ($l <= $u) {
		my $i = int(($l/24 + $u/24) / 2) * 24;
		sysseek($fh, $i, SEEK_SET) or croak "seek: $!";
		sysread($fh, my $buf, 24) == 24 or croak "read: $!";
		my ($r, $c) = unpack(rev_map_fmt, $buf);

		if ($r < $rev) {
			$l = $i + 24;
		} elsif ($r > $rev) {
			$u = $i - 24;
		} else { # $r == $rev
			return $c eq ('0' x 40) ? undef : $c;
		}
	}
	undef;
}

# Finds the first svn revision that exists on (if $eq_ok is true) or
# before $rev for the current branch.  It will not search any lower
# than $min_rev.  Returns the git commit hash and svn revision number
# if found, else (undef, undef).
sub find_rev_before {
	my ($self, $rev, $eq_ok, $min_rev) = @_;
	--$rev unless $eq_ok;
	$min_rev ||= 1;
	my $max_rev = $self->rev_map_max;
	$rev = $max_rev if ($rev > $max_rev);
	while ($rev >= $min_rev) {
		if (my $c = $self->rev_map_get($rev)) {
			return ($rev, $c);
		}
		--$rev;
	}
	return (undef, undef);
}

# Finds the first svn revision that exists on (if $eq_ok is true) or
# after $rev for the current branch.  It will not search any higher
# than $max_rev.  Returns the git commit hash and svn revision number
# if found, else (undef, undef).
sub find_rev_after {
	my ($self, $rev, $eq_ok, $max_rev) = @_;
	++$rev unless $eq_ok;
	$max_rev ||= $self->rev_map_max;
	while ($rev <= $max_rev) {
		if (my $c = $self->rev_map_get($rev)) {
			return ($rev, $c);
		}
		++$rev;
	}
	return (undef, undef);
}

sub _new {
	my ($class, $repo_id, $ref_id, $path) = @_;
	unless (defined $repo_id && length $repo_id) {
		$repo_id = $Git::SVN::default_repo_id;
	}
	unless (defined $ref_id && length $ref_id) {
		$_prefix = '' unless defined($_prefix);
		$_[2] = $ref_id =
		             "refs/remotes/$_prefix$Git::SVN::default_ref_id";
	}
	$_[1] = $repo_id;
	my $dir = "$ENV{GIT_DIR}/svn/$ref_id";

	# Older repos imported by us used $GIT_DIR/svn/foo instead of
	# $GIT_DIR/svn/refs/remotes/foo when tracking refs/remotes/foo
	if ($ref_id =~ m{^refs/remotes/(.*)}) {
		my $old_dir = "$ENV{GIT_DIR}/svn/$1";
		if (-d $old_dir && ! -d $dir) {
			$dir = $old_dir;
		}
	}

	$_[3] = $path = '' unless (defined $path);
	mkpath([$dir]);
	bless {
		ref_id => $ref_id, dir => $dir, index => "$dir/index",
	        path => $path, config => "$ENV{GIT_DIR}/svn/config",
	        map_root => "$dir/.rev_map", repo_id => $repo_id }, $class;
}

# for read-only access of old .rev_db formats
sub unlink_rev_db_symlink {
	my ($self) = @_;
	my $link = $self->rev_db_path;
	$link =~ s/\.[\w-]+$// or croak "missing UUID at the end of $link";
	if (-l $link) {
		unlink $link or croak "unlink: $link failed!";
	}
}

sub rev_db_path {
	my ($self, $uuid) = @_;
	my $db_path = $self->map_path($uuid);
	$db_path =~ s{/\.rev_map\.}{/\.rev_db\.}
	    or croak "map_path: $db_path does not contain '/.rev_map.' !";
	$db_path;
}

# the new replacement for .rev_db
sub map_path {
	my ($self, $uuid) = @_;
	$uuid ||= $self->ra_uuid;
	"$self->{map_root}.$uuid";
}

sub uri_encode {
	my ($f) = @_;
	$f =~ s#([^a-zA-Z0-9\*!\:_\./\-])#uc sprintf("%%%02x",ord($1))#eg;
	$f
}

sub uri_decode {
	my ($f) = @_;
	$f =~ s#%([0-9a-fA-F]{2})#chr(hex($1))#eg;
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
	{
		no warnings 'once';
		# All variables SVN::Auth::SSL::* are used only once,
		# so we're shutting up Perl warnings about this.
		if ($failures & $SVN::Auth::SSL::UNKNOWNCA) {
			print STDERR " - The certificate is not issued ",
			    "by a trusted authority. Use the\n",
			    "   fingerprint to validate ",
			    "the certificate manually!\n";
		}
		if ($failures & $SVN::Auth::SSL::CNMISMATCH) {
			print STDERR " - The certificate hostname ",
			    "does not match.\n";
		}
		if ($failures & $SVN::Auth::SSL::NOTYETVALID) {
			print STDERR " - The certificate is not yet valid.\n";
		}
		if ($failures & $SVN::Auth::SSL::EXPIRED) {
			print STDERR " - The certificate has expired.\n";
		}
		if ($failures & $SVN::Auth::SSL::OTHER) {
			print STDERR " - The certificate has ",
			    "an unknown error.\n";
		}
	} # no warnings 'once'
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
	my $password = '';
	if (exists $ENV{GIT_ASKPASS}) {
		open(PH, "-|", $ENV{GIT_ASKPASS}, $prompt);
		$password = <PH>;
		$password =~ s/[\012\015]//; # \n\r
		close(PH);
	} else {
		print STDERR $prompt;
		STDERR->flush;
		require Term::ReadKey;
		Term::ReadKey::ReadMode('noecho');
		while (defined(my $key = Term::ReadKey::ReadKey(0))) {
			last if $key =~ /[\012\015]/; # \n\r
			$password .= $key;
		}
		Term::ReadKey::ReadMode('restore');
		print STDERR "\n";
		STDERR->flush;
	}
	$password;
}

package SVN::Git::Fetcher;
use vars qw/@ISA $_ignore_regex $_preserve_empty_dirs $_placeholder_filename
            @deleted_gpath %added_placeholder $repo_id/;
use strict;
use warnings;
use Carp qw/croak/;
use File::Basename qw/dirname/;
use IO::File qw//;

# file baton members: path, mode_a, mode_b, pool, fh, blob, base
sub new {
	my ($class, $git_svn, $switch_path) = @_;
	my $self = SVN::Delta::Editor->new;
	bless $self, $class;
	if (exists $git_svn->{last_commit}) {
		$self->{c} = $git_svn->{last_commit};
		$self->{empty_symlinks} =
		                  _mark_empty_symlinks($git_svn, $switch_path);
	}

	# some options are read globally, but can be overridden locally
	# per [svn-remote "..."] section.  Command-line options will *NOT*
	# override options set in an [svn-remote "..."] section
	$repo_id = $git_svn->{repo_id};
	my $k = "svn-remote.$repo_id.ignore-paths";
	my $v = eval { command_oneline('config', '--get', $k) };
	$self->{ignore_regex} = $v;

	$k = "svn-remote.$repo_id.preserve-empty-dirs";
	$v = eval { command_oneline('config', '--get', '--bool', $k) };
	if ($v && $v eq 'true') {
		$_preserve_empty_dirs = 1;
		$k = "svn-remote.$repo_id.placeholder-filename";
		$v = eval { command_oneline('config', '--get', $k) };
		$_placeholder_filename = $v;
	}

	# Load the list of placeholder files added during previous invocations.
	$k = "svn-remote.$repo_id.added-placeholder";
	$v = eval { command_oneline('config', '--get-all', $k) };
	if ($_preserve_empty_dirs && $v) {
		# command() prints errors to stderr, so we only call it if
		# command_oneline() succeeded.
		my @v = command('config', '--get-all', $k);
		$added_placeholder{ dirname($_) } = $_ foreach @v;
	}

	$self->{empty} = {};
	$self->{dir_prop} = {};
	$self->{file_prop} = {};
	$self->{absent_dir} = {};
	$self->{absent_file} = {};
	$self->{gii} = $git_svn->tmp_index_do(sub { Git::IndexInfo->new });
	$self->{pathnameencoding} = Git::config('svn.pathnameencoding');
	$self;
}

# this uses the Ra object, so it must be called before do_{switch,update},
# not inside them (when the Git::SVN::Fetcher object is passed) to
# do_{switch,update}
sub _mark_empty_symlinks {
	my ($git_svn, $switch_path) = @_;
	my $bool = Git::config_bool('svn.brokenSymlinkWorkaround');
	return {} if (!defined($bool)) || (defined($bool) && ! $bool);

	my %ret;
	my ($rev, $cmt) = $git_svn->last_rev_commit;
	return {} unless ($rev && $cmt);

	# allow the warning to be printed for each revision we fetch to
	# ensure the user sees it.  The user can also disable the workaround
	# on the repository even while git svn is running and the next
	# revision fetched will skip this expensive function.
	my $printed_warning;
	chomp(my $empty_blob = `git hash-object -t blob --stdin < /dev/null`);
	my ($ls, $ctx) = command_output_pipe(qw/ls-tree -r -z/, $cmt);
	local $/ = "\0";
	my $pfx = defined($switch_path) ? $switch_path : $git_svn->{path};
	$pfx .= '/' if length($pfx);
	while (<$ls>) {
		chomp;
		s/\A100644 blob $empty_blob\t//o or next;
		unless ($printed_warning) {
			print STDERR "Scanning for empty symlinks, ",
			             "this may take a while if you have ",
				     "many empty files\n",
				     "You may disable this with `",
				     "git config svn.brokenSymlinkWorkaround ",
				     "false'.\n",
				     "This may be done in a different ",
				     "terminal without restarting ",
				     "git svn\n";
			$printed_warning = 1;
		}
		my $path = $_;
		my (undef, $props) =
		               $git_svn->ra->get_file($pfx.$path, $rev, undef);
		if ($props->{'svn:special'}) {
			$ret{$path} = 1;
		}
	}
	command_close_pipe($ls, $ctx);
	\%ret;
}

# returns true if a given path is inside a ".git" directory
sub in_dot_git {
	$_[0] =~ m{(?:^|/)\.git(?:/|$)};
}

# return value: 0 -- don't ignore, 1 -- ignore
sub is_path_ignored {
	my ($self, $path) = @_;
	return 1 if in_dot_git($path);
	return 1 if defined($self->{ignore_regex}) &&
	            $path =~ m!$self->{ignore_regex}!;
	return 0 unless defined($_ignore_regex);
	return 1 if $path =~ m!$_ignore_regex!o;
	return 0;
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
	if (my $enc = $self->{pathnameencoding}) {
		require Encode;
		Encode::from_to($path, 'UTF-8', $enc);
	}
	if ($self->{path_strip}) {
		$path =~ s!$self->{path_strip}!! or
		  die "Failed to strip path '$path' ($self->{path_strip})\n";
	}
	$path;
}

sub delete_entry {
	my ($self, $path, $rev, $pb) = @_;
	return undef if $self->is_path_ignored($path);

	my $gpath = $self->git_path($path);
	return undef if ($gpath eq '');

	# remove entire directories.
	my ($tree) = (command('ls-tree', '-z', $self->{c}, "./$gpath")
	                 =~ /\A040000 tree ([a-f\d]{40})\t\Q$gpath\E\0/);
	if ($tree) {
		my ($ls, $ctx) = command_output_pipe(qw/ls-tree
		                                     -r --name-only -z/,
				                     $tree);
		local $/ = "\0";
		while (<$ls>) {
			chomp;
			my $rmpath = "$gpath/$_";
			$self->{gii}->remove($rmpath);
			print "\tD\t$rmpath\n" unless $::_q;
		}
		print "\tD\t$gpath/\n" unless $::_q;
		command_close_pipe($ls, $ctx);
	} else {
		$self->{gii}->remove($gpath);
		print "\tD\t$gpath\n" unless $::_q;
	}
	# Don't add to @deleted_gpath if we're deleting a placeholder file.
	push @deleted_gpath, $gpath unless $added_placeholder{dirname($path)};
	$self->{empty}->{$path} = 0;
	undef;
}

sub open_file {
	my ($self, $path, $pb, $rev) = @_;
	my ($mode, $blob);

	goto out if $self->is_path_ignored($path);

	my $gpath = $self->git_path($path);
	($mode, $blob) = (command('ls-tree', '-z', $self->{c}, "./$gpath")
	                     =~ /\A(\d{6}) blob ([a-f\d]{40})\t\Q$gpath\E\0/);
	unless (defined $mode && defined $blob) {
		die "$path was not found in commit $self->{c} (r$rev)\n";
	}
	if ($mode eq '100644' && $self->{empty_symlinks}->{$path}) {
		$mode = '120000';
	}
out:
	{ path => $path, mode_a => $mode, mode_b => $mode, blob => $blob,
	  pool => SVN::Pool->new, action => 'M' };
}

sub add_file {
	my ($self, $path, $pb, $cp_path, $cp_rev) = @_;
	my $mode;

	if (!$self->is_path_ignored($path)) {
		my ($dir, $file) = ($path =~ m#^(.*?)/?([^/]+)$#);
		delete $self->{empty}->{$dir};
		$mode = '100644';

		if ($added_placeholder{$dir}) {
			# Remove our placeholder file, if we created one.
			delete_entry($self, $added_placeholder{$dir})
				unless $path eq $added_placeholder{$dir};
			delete $added_placeholder{$dir}
		}
	}

	{ path => $path, mode_a => $mode, mode_b => $mode,
	  pool => SVN::Pool->new, action => 'A' };
}

sub add_directory {
	my ($self, $path, $cp_path, $cp_rev) = @_;
	goto out if $self->is_path_ignored($path);
	my $gpath = $self->git_path($path);
	if ($gpath eq '') {
		my ($ls, $ctx) = command_output_pipe(qw/ls-tree
		                                     -r --name-only -z/,
				                     $self->{c});
		local $/ = "\0";
		while (<$ls>) {
			chomp;
			$self->{gii}->remove($_);
			print "\tD\t$_\n" unless $::_q;
			push @deleted_gpath, $gpath;
		}
		command_close_pipe($ls, $ctx);
		$self->{empty}->{$path} = 0;
	}
	my ($dir, $file) = ($path =~ m#^(.*?)/?([^/]+)$#);
	delete $self->{empty}->{$dir};
	$self->{empty}->{$path} = 1;

	if ($added_placeholder{$dir}) {
		# Remove our placeholder file, if we created one.
		delete_entry($self, $added_placeholder{$dir});
		delete $added_placeholder{$dir}
	}

out:
	{ path => $path };
}

sub change_dir_prop {
	my ($self, $db, $prop, $value) = @_;
	return undef if $self->is_path_ignored($db->{path});
	$self->{dir_prop}->{$db->{path}} ||= {};
	$self->{dir_prop}->{$db->{path}}->{$prop} = $value;
	undef;
}

sub absent_directory {
	my ($self, $path, $pb) = @_;
	return undef if $self->is_path_ignored($path);
	$self->{absent_dir}->{$pb->{path}} ||= [];
	push @{$self->{absent_dir}->{$pb->{path}}}, $path;
	undef;
}

sub absent_file {
	my ($self, $path, $pb) = @_;
	return undef if $self->is_path_ignored($path);
	$self->{absent_file}->{$pb->{path}} ||= [];
	push @{$self->{absent_file}->{$pb->{path}}}, $path;
	undef;
}

sub change_file_prop {
	my ($self, $fb, $prop, $value) = @_;
	return undef if $self->is_path_ignored($fb->{path});
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
	return undef if $self->is_path_ignored($fb->{path});
	my $fh = $::_repository->temp_acquire('svn_delta');
	# $fh gets auto-closed() by SVN::TxDelta::apply(),
	# (but $base does not,) so dup() it for reading in close_file
	open my $dup, '<&', $fh or croak $!;
	my $base = $::_repository->temp_acquire('git_blob');

	if ($fb->{blob}) {
		my ($base_is_link, $size);

		if ($fb->{mode_a} eq '120000' &&
		    ! $self->{empty_symlinks}->{$fb->{path}}) {
			print $base 'link ' or die "print $!\n";
			$base_is_link = 1;
		}
	retry:
		$size = $::_repository->cat_blob($fb->{blob}, $base);
		die "Failed to read object $fb->{blob}" if ($size < 0);

		if (defined $exp) {
			seek $base, 0, 0 or croak $!;
			my $got = ::md5sum($base);
			if ($got ne $exp) {
				my $err = "Checksum mismatch: ".
				       "$fb->{path} $fb->{blob}\n" .
				       "expected: $exp\n" .
				       "     got: $got\n";
				if ($base_is_link) {
					warn $err,
					     "Retrying... (possibly ",
					     "a bad symlink from SVN)\n";
					$::_repository->temp_reset($base);
					$base_is_link = 0;
					goto retry;
				}
				die $err;
			}
		}
	}
	seek $base, 0, 0 or croak $!;
	$fb->{fh} = $fh;
	$fb->{base} = $base;
	[ SVN::TxDelta::apply($base, $dup, undef, $fb->{path}, $fb->{pool}) ];
}

sub close_file {
	my ($self, $fb, $exp) = @_;
	return undef if $self->is_path_ignored($fb->{path});

	my $hash;
	my $path = $self->git_path($fb->{path});
	if (my $fh = $fb->{fh}) {
		if (defined $exp) {
			seek($fh, 0, 0) or croak $!;
			my $got = ::md5sum($fh);
			if ($got ne $exp) {
				die "Checksum mismatch: $path\n",
				    "expected: $exp\n    got: $got\n";
			}
		}
		if ($fb->{mode_b} == 120000) {
			sysseek($fh, 0, 0) or croak $!;
			my $rd = sysread($fh, my $buf, 5);

			if (!defined $rd) {
				croak "sysread: $!\n";
			} elsif ($rd == 0) {
				warn "$path has mode 120000",
				     " but it points to nothing\n",
				     "converting to an empty file with mode",
				     " 100644\n";
				$fb->{mode_b} = '100644';
			} elsif ($buf ne 'link ') {
				warn "$path has mode 120000",
				     " but is not a link\n";
			} else {
				my $tmp_fh = $::_repository->temp_acquire(
					'svn_hash');
				my $res;
				while ($res = sysread($fh, my $str, 1024)) {
					my $out = syswrite($tmp_fh, $str, $res);
					defined($out) && $out == $res
						or croak("write ",
							Git::temp_path($tmp_fh),
							": $!\n");
				}
				defined $res or croak $!;

				($fh, $tmp_fh) = ($tmp_fh, $fh);
				Git::temp_release($tmp_fh, 1);
			}
		}

		$hash = $::_repository->hash_and_insert_object(
				Git::temp_path($fh));
		$hash =~ /^[a-f\d]{40}$/ or die "not a sha1: $hash\n";

		Git::temp_release($fb->{base}, 1);
		Git::temp_release($fh, 1);
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

	if ($_preserve_empty_dirs) {
		my @empty_dirs;

		# Any entry flagged as empty that also has an associated
		# dir_prop represents a newly created empty directory.
		foreach my $i (keys %{$self->{empty}}) {
			push @empty_dirs, $i if exists $self->{dir_prop}->{$i};
		}

		# Search for directories that have become empty due subsequent
		# file deletes.
		push @empty_dirs, $self->find_empty_directories();

		# Finally, add a placeholder file to each empty directory.
		$self->add_placeholder_file($_) foreach (@empty_dirs);

		$self->stash_placeholder_list();
	}

	$self->{git_commit_ok} = 1;
	$self->{nr} = $self->{gii}->{nr};
	delete $self->{gii};
	$self->SUPER::close_edit(@_);
}

sub find_empty_directories {
	my ($self) = @_;
	my @empty_dirs;
	my %dirs = map { dirname($_) => 1 } @deleted_gpath;

	foreach my $dir (sort keys %dirs) {
		next if $dir eq ".";

		# If there have been any additions to this directory, there is
		# no reason to check if it is empty.
		my $skip_added = 0;
		foreach my $t (qw/dir_prop file_prop/) {
			foreach my $path (keys %{ $self->{$t} }) {
				if (exists $self->{$t}->{dirname($path)}) {
					$skip_added = 1;
					last;
				}
			}
			last if $skip_added;
		}
		next if $skip_added;

		# Use `git ls-tree` to get the filenames of this directory
		# that existed prior to this particular commit.
		my $ls = command('ls-tree', '-z', '--name-only',
				 $self->{c}, "$dir/");
		my %files = map { $_ => 1 } split(/\0/, $ls);

		# Remove the filenames that were deleted during this commit.
		delete $files{$_} foreach (@deleted_gpath);

		# Report the directory if there are no filenames left.
		push @empty_dirs, $dir unless (scalar %files);
	}
	@empty_dirs;
}

sub add_placeholder_file {
	my ($self, $dir) = @_;
	my $path = "$dir/$_placeholder_filename";
	my $gpath = $self->git_path($path);

	my $fh = $::_repository->temp_acquire($gpath);
	my $hash = $::_repository->hash_and_insert_object(Git::temp_path($fh));
	Git::temp_release($fh, 1);
	$self->{gii}->update('100644', $hash, $gpath) or croak $!;

	# The directory should no longer be considered empty.
	delete $self->{empty}->{$dir} if exists $self->{empty}->{$dir};

	# Keep track of any placeholder files we create.
	$added_placeholder{$dir} = $path;
}

sub stash_placeholder_list {
	my ($self) = @_;
	my $k = "svn-remote.$repo_id.added-placeholder";
	my $v = eval { command_oneline('config', '--get-all', $k) };
	command_noisy('config', '--unset-all', $k) if $v;
	foreach (values %added_placeholder) {
		command_noisy('config', '--add', $k, $_);
	}
}

package SVN::Git::Editor;
use vars qw/@ISA $_rmdir $_cp_similarity $_find_copies_harder $_rename_limit/;
use strict;
use warnings;
use Carp qw/croak/;
use IO::File;

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
	$self->{config} = $opts->{config};
	$self->{mergeinfo} = $opts->{mergeinfo};
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
					($::sha1)\s($::sha1)\s
					([MTCRAD])\d*$/xo) {
			push @mods, {	mode_a => $1, mode_b => $2,
					sha1_a => $3, sha1_b => $4,
					chg => $5 };
			if ($5 =~ /^(?:C|R)$/) {
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
	if (my $enc = $self->{pathnameencoding}) {
		require Encode;
		Encode::from_to($path, $enc, 'UTF-8');
	}
	$self->{path_prefix}.(defined $path ? $path : '');
}

sub url_path {
	my ($self, $path) = @_;
	if ($self->{url} =~ m#^https?://#) {
		$path =~ s!([^~a-zA-Z0-9_./-])!uc sprintf("%%%02x",ord($1))!eg;
	}
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
	{
		no warnings 'once';
		# SVN::Node::none and SVN::Node::file are used only once,
		# so we're shutting up Perl's warnings about them.
		if ($t == $SVN::Node::none) {
			return $self->add_directory($full_path, $baton,
			    undef, -1, $self->{pool});
		} elsif ($t == $SVN::Node::dir) {
			return $self->open_directory($full_path, $baton,
			    $self->{r}, $self->{pool});
		} # no warnings 'once'
		print STDERR "$full_path already exists in repository at ",
		    "r$self->{r} and it is not a directory (",
		    ($t == $SVN::Node::file ? 'file' : 'unknown'),"/$t)\n";
	} # no warnings 'once'
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

# Subroutine to convert a globbing pattern to a regular expression.
# From perl cookbook.
sub glob2pat {
	my $globstr = shift;
	my %patmap = ('*' => '.*', '?' => '.', '[' => '[', ']' => ']');
	$globstr =~ s{(.)} { $patmap{$1} || "\Q$1" }ge;
	return '^' . $globstr . '$';
}

sub check_autoprop {
	my ($self, $pattern, $properties, $file, $fbat) = @_;
	# Convert the globbing pattern to a regular expression.
	my $regex = glob2pat($pattern);
	# Check if the pattern matches the file name.
	if($file =~ m/($regex)/) {
		# Parse the list of properties to set.
		my @props = split(/;/, $properties);
		foreach my $prop (@props) {
			# Parse 'name=value' syntax and set the property.
			if ($prop =~ /([^=]+)=(.*)/) {
				my ($n,$v) = ($1,$2);
				for ($n, $v) {
					s/^\s+//; s/\s+$//;
				}
				$self->change_file_prop($fbat, $n, $v);
			}
		}
	}
}

sub apply_autoprops {
	my ($self, $file, $fbat) = @_;
	my $conf_t = ${$self->{config}}{'config'};
	no warnings 'once';
	# Check [miscellany]/enable-auto-props in svn configuration.
	if (SVN::_Core::svn_config_get_bool(
		$conf_t,
		$SVN::_Core::SVN_CONFIG_SECTION_MISCELLANY,
		$SVN::_Core::SVN_CONFIG_OPTION_ENABLE_AUTO_PROPS,
		0)) {
		# Auto-props are enabled.  Enumerate them to look for matches.
		my $callback = sub {
			$self->check_autoprop($_[0], $_[1], $file, $fbat);
		};
		SVN::_Core::svn_config_enumerate(
			$conf_t,
			$SVN::_Core::SVN_CONFIG_SECTION_AUTO_PROPS,
			$callback);
	}
}

sub A {
	my ($self, $m) = @_;
	my ($dir, $file) = split_path($m->{file_b});
	my $pbat = $self->ensure_path($dir);
	my $fbat = $self->add_file($self->repo_path($m->{file_b}), $pbat,
					undef, -1);
	print "\tA\t$m->{file_b}\n" unless $::_q;
	$self->apply_autoprops($file, $fbat);
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
	$self->apply_autoprops($file, $fbat);
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

sub change_dir_prop {
	my ($self, $pbat, $pname, $pval) = @_;
	$self->SUPER::change_dir_prop($pbat, $pname, $pval, $self->{pool});
}

sub _chg_file_get_blob ($$$$) {
	my ($self, $fbat, $m, $which) = @_;
	my $fh = $::_repository->temp_acquire("git_blob_$which");
	if ($m->{"mode_$which"} =~ /^120/) {
		print $fh 'link ' or croak $!;
		$self->change_file_prop($fbat,'svn:special','*');
	} elsif ($m->{mode_a} =~ /^120/ && $m->{"mode_$which"} !~ /^120/) {
		$self->change_file_prop($fbat,'svn:special',undef);
	}
	my $blob = $m->{"sha1_$which"};
	return ($fh,) if ($blob =~ /^0{40}$/);
	my $size = $::_repository->cat_blob($blob, $fh);
	croak "Failed to read object $blob" if ($size < 0);
	$fh->flush == 0 or croak $!;
	seek $fh, 0, 0 or croak $!;

	my $exp = ::md5sum($fh);
	seek $fh, 0, 0 or croak $!;
	return ($fh, $exp);
}

sub chg_file {
	my ($self, $fbat, $m) = @_;
	if ($m->{mode_b} =~ /755$/ && $m->{mode_a} !~ /755$/) {
		$self->change_file_prop($fbat,'svn:executable','*');
	} elsif ($m->{mode_b} !~ /755$/ && $m->{mode_a} =~ /755$/) {
		$self->change_file_prop($fbat,'svn:executable',undef);
	}
	my ($fh_a, $exp_a) = _chg_file_get_blob $self, $fbat, $m, 'a';
	my ($fh_b, $exp_b) = _chg_file_get_blob $self, $fbat, $m, 'b';
	my $pool = SVN::Pool->new;
	my $atd = $self->apply_textdelta($fbat, $exp_a, $pool);
	if (-s $fh_a) {
		my $txstream = SVN::TxDelta::new ($fh_a, $fh_b, $pool);
		my $res = SVN::TxDelta::send_txstream($txstream, @$atd, $pool);
		if (defined $res) {
			die "Unexpected result from send_txstream: $res\n",
			    "(SVN::Core::VERSION: $SVN::Core::VERSION)\n";
		}
	} else {
		my $got = SVN::TxDelta::send_stream($fh_b, @$atd, $pool);
		die "Checksum mismatch\nexpected: $exp_b\ngot: $got\n"
		    if ($got ne $exp_b);
	}
	Git::temp_release($fh_b, 1);
	Git::temp_release($fh_a, 1);
	$pool->clear;
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
			fatal("Invalid change type: $f");
		}
	}

	if (defined($self->{mergeinfo})) {
		$self->change_dir_prop($self->{bat}{''}, "svn:mergeinfo",
			               $self->{mergeinfo});
	}
	$self->rmdirs if $_rmdir;
	if (@$mods == 0 && !defined($self->{mergeinfo})) {
		$self->abort_edit;
	} else {
		$self->close_edit;
	}
	return scalar @$mods;
}

package Git::SVN::Ra;
use vars qw/@ISA $config_dir $_ignore_refs_regex $_log_window_size/;
use strict;
use warnings;
my ($ra_invalid, $can_do_switch, %ignored_err, $RA);

BEGIN {
	# enforce temporary pool usage for some simple functions
	no strict 'refs';
	for my $f (qw/rev_proplist get_latest_revnum get_uuid get_repos_root
	              get_file/) {
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

sub _auth_providers () {
	[
	  SVN::Client::get_simple_provider(),
	  SVN::Client::get_ssl_server_trust_file_provider(),
	  SVN::Client::get_simple_prompt_provider(
	    \&Git::SVN::Prompt::simple, 2),
	  SVN::Client::get_ssl_client_cert_file_provider(),
	  SVN::Client::get_ssl_client_cert_prompt_provider(
	    \&Git::SVN::Prompt::ssl_client_cert, 2),
	  SVN::Client::get_ssl_client_cert_pw_file_provider(),
	  SVN::Client::get_ssl_client_cert_pw_prompt_provider(
	    \&Git::SVN::Prompt::ssl_client_cert_pw, 2),
	  SVN::Client::get_username_provider(),
	  SVN::Client::get_ssl_server_trust_prompt_provider(
	    \&Git::SVN::Prompt::ssl_server_trust),
	  SVN::Client::get_username_prompt_provider(
	    \&Git::SVN::Prompt::username, 2)
	]
}

sub escape_uri_only {
	my ($uri) = @_;
	my @tmp;
	foreach (split m{/}, $uri) {
		s/([^~\w.%+-]|%(?![a-fA-F0-9]{2}))/sprintf("%%%02X",ord($1))/eg;
		push @tmp, $_;
	}
	join('/', @tmp);
}

sub escape_url {
	my ($url) = @_;
	if ($url =~ m#^(https?)://([^/]+)(.*)$#) {
		my ($scheme, $domain, $uri) = ($1, $2, escape_uri_only($3));
		$url = "$scheme://$domain$uri";
	}
	$url;
}

sub new {
	my ($class, $url) = @_;
	$url =~ s!/+$!!;
	return $RA if ($RA && $RA->{url} eq $url);

	::_req_svn();

	SVN::_Core::svn_config_ensure($config_dir, undef);
	my ($baton, $callbacks) = SVN::Core::auth_open_helper(_auth_providers);
	my $config = SVN::Core::config_get_config($config_dir);
	$RA = undef;
	my $dont_store_passwords = 1;
	my $conf_t = ${$config}{'config'};
	{
		no warnings 'once';
		# The usage of $SVN::_Core::SVN_CONFIG_* variables
		# produces warnings that variables are used only once.
		# I had not found the better way to shut them up, so
		# the warnings of type 'once' are disabled in this block.
		if (SVN::_Core::svn_config_get_bool($conf_t,
		    $SVN::_Core::SVN_CONFIG_SECTION_AUTH,
		    $SVN::_Core::SVN_CONFIG_OPTION_STORE_PASSWORDS,
		    1) == 0) {
			SVN::_Core::svn_auth_set_parameter($baton,
			    $SVN::_Core::SVN_AUTH_PARAM_DONT_STORE_PASSWORDS,
			    bless (\$dont_store_passwords, "_p_void"));
		}
		if (SVN::_Core::svn_config_get_bool($conf_t,
		    $SVN::_Core::SVN_CONFIG_SECTION_AUTH,
		    $SVN::_Core::SVN_CONFIG_OPTION_STORE_AUTH_CREDS,
		    1) == 0) {
			$Git::SVN::Prompt::_no_auth_cache = 1;
		}
	} # no warnings 'once'
	my $self = SVN::Ra->new(url => escape_url($url), auth => $baton,
	                      config => $config,
			      pool => SVN::Pool->new,
	                      auth_provider_callbacks => $callbacks);
	$self->{url} = $url;
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

# get_log(paths, start, end, limit,
#         discover_changed_paths, strict_node_history, receiver)
sub get_log {
	my ($self, @args) = @_;
	my $pool = SVN::Pool->new;

	# svn_log_changed_path_t objects passed to get_log are likely to be
	# overwritten even if only the refs are copied to an external variable,
	# so we should dup the structures in their entirety.  Using an
	# externally passed pool (instead of our temporary and quickly cleared
	# pool in Git::SVN::Ra) does not help matters at all...
	my $receiver = pop @args;
	my $prefix = "/".$self->{svn_path};
	$prefix =~ s#/+($)##;
	my $prefix_regex = qr#^\Q$prefix\E#;
	push(@args, sub {
		my ($paths) = $_[0];
		return &$receiver(@_) unless $paths;
		$_[0] = ();
		foreach my $p (keys %$paths) {
			my $i = $paths->{$p};
			# Make path relative to our url, not repos_root
			$p =~ s/$prefix_regex//;
			my %s = map { $_ => $i->$_; }
				qw/copyfrom_path copyfrom_rev action/;
			if ($s{'copyfrom_path'}) {
				$s{'copyfrom_path'} =~ s/$prefix_regex//;
			}
			$_[0]{$p} = \%s;
		}
		&$receiver(@_);
	});


	# the limit parameter was not supported in SVN 1.1.x, so we
	# drop it.  Therefore, the receiver callback passed to it
	# is made aware of this limitation by being wrapped if
	# the limit passed to is being wrapped.
	if ($SVN::Core::VERSION le '1.2.0') {
		my $limit = splice(@args, 3, 1);
		if ($limit > 0) {
			my $receiver = pop @args;
			push(@args, sub { &$receiver(@_) if (--$limit >= 0) });
		}
	}
	my $ret = $self->SUPER::get_log(@args, $pool);
	$pool->clear;
	$ret;
}

sub trees_match {
	my ($self, $url1, $rev1, $url2, $rev2) = @_;
	my $ctx = SVN::Client->new(auth => _auth_providers);
	my $out = IO::File->new_tmpfile;

	# older SVN (1.1.x) doesn't take $pool as the last parameter for
	# $ctx->diff(), so we'll create a default one
	my $pool = SVN::Pool->new_default_sub;

	$ra_invalid = 1; # this will open a new SVN::Ra connection to $url1
	$ctx->diff([], $url1, $rev1, $url2, $rev2, 1, 1, 0, $out, $out);
	$out->flush;
	my $ret = (($out->stat)[7] == 0);
	close $out or croak $!;

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
	$full_url .= '/' . $path if length $path;
	my ($ra, $reparented);

	if ($old_url =~ m#^svn(\+ssh)?://# ||
	    ($full_url =~ m#^https?://# &&
	     escape_url($full_url) ne $full_url)) {
		$_[0] = undef;
		$self = undef;
		$RA = undef;
		$ra = Git::SVN::Ra->new($full_url);
		$ra_invalid = 1;
	} elsif ($old_url ne $full_url) {
		SVN::_Ra::svn_ra_reparent($self->{session}, $full_url, $pool);
		$self->{url} = $full_url;
		$reparented = 1;
	}

	$ra ||= $self;
	$url_b = escape_url($url_b);
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
	my $ra_url = $self->{url};
	my $find_trailing_edge;
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
			[ $paths,
			  { author => $author, date => $date, log => $log } ];
		}
		$self->get_log([$longest_path], $min, $max, 0, 1, 1,
		               sub { $revs{$_[1]} = _cb(@_) });
		if ($err) {
			print "Checked through r$max\r";
		} else {
			$find_trailing_edge = 1;
		}
		if ($err and $find_trailing_edge) {
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
				               $ok = $_[1];
				               $revs{$_[1]} = _cb(@_) });
				if ($ok) {
					print STDERR "r$min .. r$ok OK\n";
					last;
				}
			}
			$find_trailing_edge = 0;
		}
		$SVN::Error::handler = $err_handler;

		my %exists = map { $_->{path} => $_ } @$gsv;
		foreach my $r (sort {$a <=> $b} keys %revs) {
			my ($paths, $logged) = @{$revs{$r}};

			foreach my $gs ($self->match_globs(\%exists, $paths,
			                                   $globs, $r)) {
				if ($gs->rev_map_max >= $r) {
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
				$INDEX_FILES{$gs->{index}} = 1;
			}
			foreach my $g (@$globs) {
				my $k = "svn-remote.$g->{remote}." .
				        "$g->{t}-maxRev";
				Git::SVN::tmp_config($k, $r);
			}
			if ($ra_invalid) {
				$_[0] = undef;
				$self = undef;
				$RA = undef;
				$self = Git::SVN::Ra->new($ra_url);
				$ra_invalid = undef;
			}
		}
		# pre-fill the .rev_db since it'll eventually get filled in
		# with '0' x40 if something new gets committed
		foreach my $gs (@$gsv) {
			next if $gs->rev_map_max >= $max;
			next if defined $gs->rev_map_get($max);
			$gs->rev_map_set($max, 0 x40);
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
	Git::SVN::gc();
}

sub get_dir_globbed {
	my ($self, $left, $depth, $r) = @_;

	my @x = eval { $self->get_dir($left, $r) };
	return unless scalar @x == 3;
	my $dirents = $x[0];
	my @finalents;
	foreach my $de (keys %$dirents) {
		next if $dirents->{$de}->{kind} != $SVN::Node::dir;
		if ($depth > 1) {
			my @args = ("$left/$de", $depth - 1, $r);
			foreach my $dir ($self->get_dir_globbed(@args)) {
				push @finalents, "$de/$dir";
			}
		} else {
			push @finalents, $de;
		}
	}
	@finalents;
}

# return value: 0 -- don't ignore, 1 -- ignore
sub is_ref_ignored {
	my ($g, $p) = @_;
	my $refname = $g->{ref}->full_path($p);
	return 1 if defined($g->{ignore_refs_regex}) &&
	            $refname =~ m!$g->{ignore_refs_regex}!;
	return 0 unless defined($_ignore_refs_regex);
	return 1 if $refname =~ m!$_ignore_refs_regex!o;
	return 0;
}

sub match_globs {
	my ($self, $exists, $paths, $globs, $r) = @_;

	sub get_dir_check {
		my ($self, $exists, $g, $r) = @_;

		my @dirs = $self->get_dir_globbed($g->{path}->{left},
		                                  $g->{path}->{depth},
		                                  $r);

		foreach my $de (@dirs) {
			my $p = $g->{path}->full_path($de);
			next if $exists->{$p};
			next if (length $g->{path}->{right} &&
				 ($self->check_path($p, $r) !=
				  $SVN::Node::dir));
			next unless $p =~ /$g->{path}->{regex}/;
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
			next if is_ref_ignored($g, $p);
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
		eval {
			my $ra = (ref $self)->new($url);
			my $latest = $ra->get_latest_revnum;
			$ra->get_log("", $latest, 0, 1, 0, 1, sub {});
		};
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
			warn "W: Do not be alarmed at the above message ",
			     "git-svn is just searching aggressively for ",
			     "old history.\n",
			     "This may take a while on large repositories\n";
			$ignored_err{$err_key} = 1;
		}
		return;
	}
	die "Error from SVN, ($errno): ", $err->expanded_message,"\n";
}

package Git::SVN::Log;
use strict;
use warnings;
use POSIX qw/strftime/;
use Time::Local;
use constant commit_log_separator => ('-' x 72) . "\n";
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
	return $color || Git->repository->get_colorbool('color.diff');
}

sub git_svn_log_cmd {
	my ($r_min, $r_max, @args) = @_;
	my $head = 'HEAD';
	my (@files, @log_opts);
	foreach my $x (@args) {
		if ($x eq '--' || @files) {
			push @files, $x;
		} else {
			if (::verify_ref("$x^0")) {
				$head = $x;
			} else {
				push @log_opts, $x;
			}
		}
	}

	my ($url, $rev, $uuid, $gs) = ::working_head_info($head);
	$gs ||= Git::SVN->_new;
	my @cmd = (qw/log --abbrev-commit --pretty=raw --default/,
	           $gs->refname);
	push @cmd, '-r' unless $non_recursive;
	push @cmd, qw/--raw --name-status/ if $verbose;
	push @cmd, '--color' if log_use_color();
	push @cmd, @log_opts;
	if (defined $r_max && $r_max == $r_min) {
		push @cmd, '--max-count=1';
		if (my $c = $gs->rev_map_get($r_max)) {
			push @cmd, $c;
		}
	} elsif (defined $r_max) {
		if ($r_max < $r_min) {
			($r_min, $r_max) = ($r_max, $r_min);
		}
		my (undef, $c_max) = $gs->find_rev_before($r_max, 1, $r_min);
		my (undef, $c_min) = $gs->find_rev_after($r_min, 1, $r_max);
		# If there are no commits in the range, both $c_max and $c_min
		# will be undefined.  If there is at least 1 commit in the
		# range, both will be defined.
		return () if !defined $c_min || !defined $c_max;
		if ($c_min eq $c_max) {
			push @cmd, '--max-count=1', $c_min;
		} else {
			push @cmd, '--boundary', "$c_min..$c_max";
		}
	}
	return (@cmd, @files);
}

# adapted from pager.c
sub config_pager {
	if (! -t *STDOUT) {
		$ENV{GIT_PAGER_IN_USE} = 'false';
		$pager = undef;
		return;
	}
	chomp($pager = command_oneline(qw(var GIT_PAGER)));
	if ($pager eq 'cat') {
		$pager = undef;
	}
	$ENV{GIT_PAGER_IN_USE} = defined($pager);
}

sub run_pager {
	return unless defined $pager;
	pipe my ($rfd, $wfd) or return;
	defined(my $pid = fork) or ::fatal "Can't fork: $!";
	if (!$pid) {
		open STDOUT, '>&', $wfd or
		                     ::fatal "Can't redirect to stdout: $!";
		return;
	}
	open STDIN, '<&', $rfd or ::fatal "Can't redirect stdin: $!";
	$ENV{LESS} ||= 'FRSX';
	exec $pager or ::fatal "Can't run pager: $! ($pager)";
}

sub format_svn_date {
	# some systmes don't handle or mishandle %z, so be creative.
	my $t = shift || time;
	my $gm = timelocal(gmtime($t));
	my $sign = qw( + + - )[ $t <=> $gm ];
	my $gmoff = sprintf("%s%02d%02d", $sign, (gmtime(abs($t - $gm)))[2,1]);
	return strftime("%Y-%m-%d %H:%M:%S $gmoff (%a, %d %b %Y)", localtime($t));
}

sub parse_git_date {
	my ($t, $tz) = @_;
	# Date::Parse isn't in the standard Perl distro :(
	if ($tz =~ s/^\+//) {
		$t += tz_to_s_offset($tz);
	} elsif ($tz =~ s/^\-//) {
		$t -= tz_to_s_offset($tz);
	}
	return $t;
}

sub set_local_timezone {
	if (defined $TZ) {
		$ENV{TZ} = $TZ;
	} else {
		delete $ENV{TZ};
	}
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
	$dest->{t_utc} = parse_git_date($t, $tz);
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
	print commit_log_separator, "r$c->{r} | ";
	print "$c->{c} | " if $show_commit;
	print "$c->{a} | ", format_svn_date($c->{t_utc}), ' | ';
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
	set_local_timezone();
	if (defined $::_revision) {
		if ($::_revision =~ /^(\d+):(\d+)$/) {
			($r_min, $r_max) = ($1, $2);
		} elsif ($::_revision =~ /^\d+$/) {
			$r_min = $r_max = $::_revision;
		} else {
			::fatal "-r$::_revision is not supported, use ",
				"standard 'git log' arguments instead";
		}
	}

	config_pager();
	@args = git_svn_log_cmd($r_min, $r_max, @args);
	if (!@args) {
		print commit_log_separator unless $incremental || $oneline;
		return;
	}
	my $log = command_output_pipe(@args);
	run_pager();
	my (@k, $c, $d, $stat);
	my $esc_color = qr/(?:\033\[(?:(?:\d+;)*\d*)?m)*/;
	while (<$log>) {
		if (/^${esc_color}commit (?:- )?($::sha1_short)/o) {
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
		($r_min, $r_max) = ($r_max, $r_min);
		process_commit($_, $r_min, $r_max) foreach reverse @k;
	}
out:
	close $log;
	print commit_log_separator unless $incremental || $oneline;
}

sub cmd_blame {
	my $path = pop;

	config_pager();
	run_pager();

	my ($fh, $ctx, $rev);

	if ($_git_format) {
		($fh, $ctx) = command_output_pipe('blame', @_, $path);
		while (my $line = <$fh>) {
			if ($line =~ /^\^?([[:xdigit:]]+)\s/) {
				# Uncommitted edits show up as a rev ID of
				# all zeros, which we can't look up with
				# cmt_metadata
				if ($1 !~ /^0+$/) {
					(undef, $rev, undef) =
						::cmt_metadata($1);
					$rev = '0' if (!$rev);
				} else {
					$rev = '0';
				}
				$rev = sprintf('%-10s', $rev);
				$line =~ s/^\^?[[:xdigit:]]+(\s)/$rev$1/;
			}
			print $line;
		}
	} else {
		($fh, $ctx) = command_output_pipe('blame', '-p', @_, 'HEAD',
						  '--', $path);
		my ($sha1);
		my %authors;
		my @buffer;
		my %dsha; #distinct sha keys

		while (my $line = <$fh>) {
			push @buffer, $line;
			if ($line =~ /^([[:xdigit:]]{40})\s\d+\s\d+/) {
				$dsha{$1} = 1;
			}
		}

		my $s2r = ::cmt_sha2rev_batch([keys %dsha]);

		foreach my $line (@buffer) {
			if ($line =~ /^([[:xdigit:]]{40})\s\d+\s\d+/) {
				$rev = $s2r->{$1};
				$rev = '0' if (!$rev)
			}
			elsif ($line =~ /^author (.*)/) {
				$authors{$rev} = $1;
				$authors{$rev} =~ s/\s/_/g;
			}
			elsif ($line =~ /^\t(.*)$/) {
				printf("%6s %10s %s\n", $rev, $authors{$rev}, $1);
			}
		}
	}
	command_close_pipe($fh, $ctx);
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
#
# v5 layout: .rev_db.$UUID => .rev_map.$UUID
#            - newer, more-efficient format that uses 24-bytes per record
#              with no filler space.
#            - use xxd -c24 < .rev_map.$UUID to view and debug
#            - This is a one-way migration, repositories updated to the
#              new format will not be able to use old git-svn without
#              rebuilding the .rev_db.  Rebuilding the rev_db is not
#              possible if noMetadata or useSvmProps are set; but should
#              be no problem for users that use the (sensible) defaults.
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
	             "($::VERSION) of git-svn) does not exist.\n";
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
		    ($ra->{repos_root} eq $repo_id)) {
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
		my $repo_id = $root_repos->{$url} || $url;

		my $fetch = $new_urls->{$url};
		foreach my $path (keys %$fetch) {
			my $x = $fetch->{$path};
			Git::SVN->init($url, $path, $repo_id, $x->{ref_id});
			my $pfx = "svn-remote.$x->{old_repo_id}";

			my $old_fetch = quotemeta("$x->{old_path}:".
			                          "$x->{ref_id}");
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
		my $file = $ENV{GIT_CONFIG} || "$ENV{GIT_DIR}/config";
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
	my ($class, $glob, $pattern_ok) = @_;
	my $re = $glob;
	$re =~ s!/+$!!g; # no need for trailing slashes
	my (@left, @right, @patterns);
	my $state = "left";
	my $die_msg = "Only one set of wildcard directories " .
				"(e.g. '*' or '*/*/*') is supported: '$glob'\n";
	for my $part (split(m|/|, $glob)) {
		if ($part =~ /\*/ && $part ne "*") {
			die "Invalid pattern in '$glob': $part\n";
		} elsif ($pattern_ok && $part =~ /[{}]/ &&
			 $part !~ /^\{[^{}]+\}/) {
			die "Invalid pattern in '$glob': $part\n";
		}
		if ($part eq "*") {
			die $die_msg if $state eq "right";
			$state = "pattern";
			push(@patterns, "[^/]*");
		} elsif ($pattern_ok && $part =~ /^\{(.*)\}$/) {
			die $die_msg if $state eq "right";
			$state = "pattern";
			my $p = quotemeta($1);
			$p =~ s/\\,/|/g;
			push(@patterns, "(?:$p)");
		} else {
			if ($state eq "left") {
				push(@left, $part);
			} else {
				push(@right, $part);
				$state = "right";
			}
		}
	}
	my $depth = @patterns;
	if ($depth == 0) {
		die "One '*' is needed in glob: '$glob'\n";
	}
	my $left = join('/', @left);
	my $right = join('/', @right);
	$re = join('/', @patterns);
	$re = join('\/',
		   grep(length, quotemeta($left), "($re)", quotemeta($right)));
	my $left_re = qr/^\/\Q$left\E(\/|$)/;
	bless { left => $left, right => $right, left_regex => $left_re,
	        regex => qr/$re/, glob => $glob, depth => $depth }, $class;
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
