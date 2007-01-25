#!/usr/bin/env perl
# Copyright (C) 2006, Eric Wong <normalperson@yhbt.net>
# License: GPL v2 or later
use warnings;
use strict;
use vars qw/	$AUTHOR $VERSION
		$SVN_URL
		$GIT_SVN_INDEX $GIT_SVN
		$GIT_DIR $GIT_SVN_DIR $REVDB
		$_follow_parent $sha1 $sha1_short $_revision
		$_cp_remote $_upgrade $_rmdir $_q $_cp_similarity
		$_find_copies_harder $_l $_authors %users/;
$AUTHOR = 'Eric Wong <normalperson@yhbt.net>';
$VERSION = '@@GIT_VERSION@@';

$ENV{GIT_DIR} ||= '.git';
$Git::SVN::default_repo_id = 'git-svn';
$Git::SVN::default_ref_id = $ENV{GIT_SVN_ID} || 'git-svn';

my $LC_ALL = $ENV{LC_ALL};
$Git::SVN::Log::TZ = $ENV{TZ};
# make sure the svn binary gives consistent output between locales and TZs:
$ENV{TZ} = 'UTC';
$ENV{LC_ALL} = 'C';
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
use Getopt::Long qw/:config gnu_getopt no_ignore_case auto_abbrev pass_through/;
use IPC::Open3;
use Git;

BEGIN {
	my $s;
	foreach (qw/command command_oneline command_noisy command_output_pipe
	            command_input_pipe command_close_pipe/) {
		$s .= "*SVN::Git::Editor::$_ = *SVN::Git::Fetcher::$_ = ".
		      "*Git::SVN::Migration::$_ = ".
		      "*Git::SVN::Log::$_ = *Git::SVN::$_ = *$_ = *Git::$_; ";
	}
	eval $s;
}

my ($SVN);

my $_optimize_commits = 1 unless $ENV{GIT_SVN_NO_OPTIMIZE_COMMITS};
$sha1 = qr/[a-f\d]{40}/;
$sha1_short = qr/[a-f\d]{4,40}/;
my ($_stdin, $_help, $_edit,
	$_repack, $_repack_nr, $_repack_flags,
	$_message, $_file, $_no_metadata,
	$_template, $_shared,
	$_version, $_upgrade,
	$_merge, $_strategy, $_dry_run,
	$_prefix);

my %remote_opts = ( 'username=s' => \$Git::SVN::Prompt::_username,
                    'config-dir=s' => \$Git::SVN::Ra::config_dir,
                    'no-auth-cache' => \$Git::SVN::Prompt::_no_auth_cache );
my %fc_opts = ( 'follow-parent|follow' => \$_follow_parent,
		'authors-file|A=s' => \$_authors,
		'repack:i' => \$_repack,
		'no-metadata' => \$_no_metadata,
		'quiet|q' => \$_q,
		'repack-flags|repack-args|repack-opts=s' => \$_repack_flags,
		%remote_opts );

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
	init => [ \&cmd_init, "Initialize a repo for tracking" .
			  " (requires URL argument)",
			  \%init_opts ],
	dcommit => [ \&cmd_dcommit,
	             'Commit several diffs to merge with upstream',
			{ 'merge|m|M' => \$_merge,
			  'strategy|s=s' => \$_strategy,
			  'dry-run|n' => \$_dry_run,
			%cmt_opts, %fc_opts } ],
	'set-tree' => [ \&cmd_set_tree,
	                "Set an SVN repository to a git tree-ish",
			{ 'stdin|' => \$_stdin, %cmt_opts, %fc_opts, } ],
	'show-ignore' => [ \&cmd_show_ignore, "Show svn:ignore listings",
			{ 'revision|r=i' => \$_revision } ],
	rebuild => [ \&cmd_rebuild, "Rebuild git-svn metadata (after git clone)",
			{ 'copy-remote|remote=s' => \$_cp_remote,
			  'upgrade' => \$_upgrade } ],
	'multi-init' => [ \&cmd_multi_init,
			'Initialize multiple trees (like git-svnimport)',
			{ %multi_opts, %init_opts, %remote_opts,
			 'revision|r=i' => \$_revision,
			 'prefix=s' => \$_prefix,
			} ],
	'multi-fetch' => [ \&cmd_multi_fetch,
			'Fetch multiple trees (like git-svnimport)',
			\%fc_opts ],
	'migrate' => [ sub { },
	               # no-op, we automatically run this anyways,
	               'Migrate configuration/metadata/layout from
		        previous versions of git-svn',
			\%remote_opts ],
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
my $rv = GetOptions(%opts, 'help|H|h' => \$_help,
				'version|V' => \$_version,
				'minimize-connections' =>
				  \$Git::SVN::Migration::_minimize,
				'id|i=s' => \$Git::SVN::default_ref_id);
exit 1 if (!$rv && $cmd ne 'log');

usage(0) if $_help;
version() if $_version;
usage(1) unless defined $cmd;
load_authors() if $_authors;
unless ($cmd =~ /^(?:init|rebuild|multi-init|commit-diff)$/) {
	Git::SVN::Migration::migration_check();
}
eval {
	Git::SVN::verify_remotes_sanity();
	$cmd{$cmd}->[0]->(@ARGV);
};
fatal $@ if $@;
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

sub cmd_rebuild {
	my $url = shift;
	my $gs = $url ? Git::SVN->init($url)
	              : eval { Git::SVN->new };
	$gs ||= Git::SVN->_new;
	if (!verify_ref($gs->refname.'^0')) {
		$gs->copy_remote_ref;
	}

	my ($rev_list, $ctx) = command_output_pipe("rev-list", $gs->refname);
	my $latest;
	my $svn_uuid;
	while (<$rev_list>) {
		chomp;
		my $c = $_;
		fatal "Non-SHA1: $c\n" unless $c =~ /^$sha1$/o;
		my ($url, $rev, $uuid) = cmt_metadata($c);

		# ignore merges (from set-tree)
		next if (!defined $rev || !$uuid);

		# if we merged or otherwise started elsewhere, this is
		# how we break out of it
		if ((defined $svn_uuid && ($uuid ne $svn_uuid)) ||
		    ($gs->{url} && $url && ($url ne $gs->{url}))) {
			next;
		}

		unless (defined $latest) {
			if (!$gs->{url} && !$url) {
				fatal "SVN repository location required\n";
			}
			$gs = Git::SVN->init($url);
			$latest = $rev;
		}
		$gs->rev_db_set($rev, $c);
		print "r$rev = $c\n";
	}
	command_close_pipe($rev_list, $ctx);
}

sub do_git_init_db {
	unless (-d $ENV{GIT_DIR}) {
		my @init_db = ('init');
		push @init_db, "--template=$_template" if defined $_template;
		push @init_db, "--shared" if defined $_shared;
		command_noisy(@init_db);
	}
}

sub cmd_init {
	my $url = shift or die "SVN repository location required " .
				"as a command-line argument\n";
	if (my $repo_path = shift) {
		unless (-d $repo_path) {
			mkpath([$repo_path]);
		}
		chdir $repo_path or croak $!;
		$ENV{GIT_DIR} = $repo_path . "/.git";
	}
	do_git_init_db();

	Git::SVN->init($url);
}

sub cmd_fetch {
	if (@_) {
		die "Additional fetch arguments are no longer supported.\n",
		    "Use --follow-parent if you have moved/copied directories
		    instead.\n";
	}
	my $gs = Git::SVN->new;
	$gs->fetch;
	if ($gs->{last_commit} && !verify_ref('refs/heads/master^0')) {
		command_noisy(qw(update-ref refs/heads/master),
		              $gs->{last_commit});
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
	my $gs = Git::SVN->new;
	$head ||= 'HEAD';
	my @refs = command(qw/rev-list --no-merges/, $gs->refname."..$head");
	my $last_rev;
	foreach my $d (reverse @refs) {
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
			my $log = get_commit_entry($d)->{log};
			my $ra = $gs->ra;
			my $pool = SVN::Pool->new;
			my %ed_opts = ( r => $last_rev,
			                ra => $ra->dup,
			                svn_path => $ra->{svn_path} );
			my $ed = SVN::Git::Editor->new(\%ed_opts,
			                 $ra->get_commit_editor($log,
			                 sub { print "Committed r$_[0]\n";
					       $last_rev = $_[0]; }),
			                 $pool);
			my $mods = $ed->apply_diff("$d~1", $d);
			if (@$mods == 0) {
				print "No changes\n$d~1 == $d\n";
			}
		}
	}
	return if $_dry_run;
	$gs->fetch;
	# we always want to rebase against the current HEAD, not any
	# head that was passed to us
	my @diff = command('diff-tree', 'HEAD', $gs->refname, '--');
	my @finish;
	if (@diff) {
		@finish = qw/rebase/;
		push @finish, qw/--merge/ if $_merge;
		push @finish, "--strategy=$_strategy" if $_strategy;
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

sub cmd_show_ignore {
	my $gs = Git::SVN->new;
	my $r = (defined $_revision ? $_revision : $gs->ra->get_latest_revnum);
	$gs->traverse_ignore(\*STDOUT, '', $r);
}

sub cmd_multi_init {
	my $url = shift;
	unless (defined $_trunk || defined $_branches || defined $_tags) {
		usage(1);
	}
	do_git_init_db();
	$_prefix = '' unless defined $_prefix;
	$url =~ s#/+$## if defined $url;
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
	my @gs;
	foreach (command(qw/config -l/)) {
		next unless m!^svn-remote\.(.+)\.fetch=
		              \s*(.*)\s*:\s*refs/remotes/(.+)\s*$!x;
		my ($repo_id, $path, $ref_id) = ($1, $2, $3);
		push @gs, Git::SVN->new($ref_id, $repo_id, $path);
	}
	foreach (@gs) {
		$_->fetch;
	}
}

# this command is special because it requires no metadata
sub cmd_commit_diff {
	my ($ta, $tb, $url) = @_;
	my $usage = "Usage: $0 commit-diff -r<revision> ".
	            "<tree-ish> <tree-ish> [<URL>]\n";
	fatal($usage) if (!defined $ta || !defined $tb);
	if (!defined $url) {
		my $gs = eval { Git::SVN->new };
		if (!$gs) {
			fatal("Needed URL or usable git-svn --id in ",
			      "the command-line\n", $usage);
		}
		$url = $gs->{url};
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
	my $r = $_revision;
	if ($r eq 'HEAD') {
		$r = $ra->get_latest_revnum;
	} elsif ($r !~ /^\d+$/) {
		die "revision argument: $r not understood by git-svn\n";
	}
	my $pool = SVN::Pool->new;
	my %ed_opts = ( r => $r,
	                ra => $ra->dup,
	                svn_path => $ra->{svn_path} );
	my $ed = SVN::Git::Editor->new(\%ed_opts,
	                               $ra->get_commit_editor($_message,
	                                 sub { print "Committed r$_[0]\n" }),
	                               $pool);
	my $mods = $ed->apply_diff($ta, $tb);
	if (@$mods == 0) {
		print "No changes\n$ta == $tb\n";
	}
	$pool->clear;
}

########################### utility functions #########################

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
	my $r = defined $_revision ? $_revision : $ra->get_latest_revnum;
	my ($dirent, undef, undef) = $ra->get_dir($repo_path, $r);
	my $url = $ra->{url};
	foreach my $d (sort keys %$dirent) {
		next if ($dirent->{$d}->kind != $SVN::Node::dir);
		my $path =  "$repo_path/$d";
		my $ref = "$pfx$d";
		my $gs = eval { Git::SVN->new($ref) };
		# don't try to init already existing refs
		unless ($gs) {
			print "init $url/$path => $ref\n";
			Git::SVN->init($url, $path, undef, $ref);
		}
	}
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
			if ($tmp && !($arg =~ / --bool/ && $tmp eq 'false')) {
				$$v = $tmp;
			}
		}
	}
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
		close $fh;
		return $s;
	}
	die "Can't get commit time for commit: $cmt\n";
}

sub tz_to_s_offset {
	my ($tz) = @_;
	$tz =~ s/(\d\d)$//;
	return ($1 * 60) + ($tz * 3600);
}

package Git::SVN;
use strict;
use warnings;
use vars qw/$default_repo_id $default_ref_id/;
use Carp qw/croak/;
use File::Path qw/mkpath/;
use IPC::Open3;

# properties that we do not log:
my %SKIP_PROP;
BEGIN {
	%SKIP_PROP = map { $_ => 1 } qw/svn:wc:ra_dav:version-url
	                                svn:special svn:executable
	                                svn:entry:committed-rev
	                                svn:entry:last-author
	                                svn:entry:uuid
	                                svn:entry:committed-date/;
}

sub read_all_remotes {
	my $r = {};
	foreach (grep { s/^svn-remote\.// } command(qw/config -l/)) {
		if (m!^(.+)\.fetch=\s*(.*)\s*:\s*refs/remotes/(.+)\s*$!) {
			$r->{$1}->{fetch}->{$2} = $3;
		} elsif (m!^(.+)\.url=\s*(.*)\s*$!) {
			$r->{$1}->{url} = $2;
		}
	}
	$r;
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
	my ($self, $url) = @_;
	$url =~ s!/+$!!; # strip trailing slash
	my $r = read_all_remotes();
	my $existing = find_existing_remote($url, $r);
	if ($existing) {
		print STDERR "Using existing ",
			     "[svn-remote \"$existing\"]\n";
		$self->{repo_id} = $existing;
	} else {
		my $min_url = Git::SVN::Ra->new($url)->minimize_url;
		$existing = find_existing_remote($min_url, $r);
		if ($existing) {
			print STDERR "Using existing ",
				     "[svn-remote \"$existing\"]\n";
			$self->{repo_id} = $existing;
		}
		if ($min_url ne $url) {
			print STDERR "Using higher level of URL: ",
			             "$url => $min_url\n";
			my $old_path = $self->{path};
			$self->{path} = $url;
			$self->{path} =~ s!^\Q$min_url\E/*!!;
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
	command_noisy('config',
		      "svn-remote.$self->{repo_id}.url", $url);
	command_noisy('config', '--add',
		      "svn-remote.$self->{repo_id}.fetch",
		      "$self->{path}:".$self->refname);
	$self->{url} = $url;
}

sub init {
	my ($class, $url, $path, $repo_id, $ref_id) = @_;
	my $self = _new($class, $repo_id, $ref_id, $path);
	if (defined $url) {
		$self->init_remote_config($url);
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
	$self;
}

sub refname { "refs/remotes/$_[0]->{ref_id}" }

sub ra {
	my ($self) = shift;
	$self->{ra} ||= Git::SVN::Ra->new($self->{url});
}

sub rel_path {
	my ($self) = @_;
	my $repos_root = $self->ra->{repos_root};
	return $self->{path} if ($self->{url} eq $repos_root);
	my $url = $self->{url} .
	          (length $self->{path} ? "/$self->{path}" : $self->{path});
	$url =~ s!^\Q$repos_root\E/*!!g;
	$url;
}

sub copy_remote_ref {
	my ($self) = @_;
	my $origin = $::_cp_remote ? $::_cp_remote : 'origin';
	my $ref = $self->refname;
	if (command('ls-remote', $origin, $ref)) {
		command_noisy('fetch', $origin, "$ref:$ref");
	} elsif ($::_cp_remote && !$::_upgrade) {
		die "Unable to find remote reference: $ref on $origin\n";
	}
}

sub traverse_ignore {
	my ($self, $fh, $path, $r) = @_;
	$path =~ s#^/+##g;
	my ($dirent, undef, $props) = $self->ra->get_dir($path, $r);
	my $p = $path;
	$p =~ s#^\Q$self->{ra}->{svn_path}\E/##;
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
		$self->traverse_ignore($fh, "$path/$_", $r);
	}
}

# returns the newest SVN revision number and newest commit SHA1
sub last_rev_commit {
	my ($self) = @_;
	if (defined $self->{last_rev} && defined $self->{last_commit}) {
		return ($self->{last_rev}, $self->{last_commit});
	}
	my $c = ::verify_ref($self->refname.'^0');
	if ($c) {
		my $rev = (::cmt_metadata($c))[1];
		if (defined $rev) {
			($self->{last_rev}, $self->{last_commit}) = ($rev, $c);
			return ($rev, $c);
		}
	}
	my $offset = -41; # from tail
	my $rl;
	open my $fh, '<', $self->{db_path} or
	                         croak "$self->{db_path} not readable: $!\n";
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
	croak $! if ($rev < 0);
	$rev =  ($rev - 41) / 41;
	close $fh or croak $!;
	($self->{last_rev}, $self->{last_commit}) = ($rev, $c);
	return ($rev, $c);
}

sub parse_revision {
	my ($self, $base) = @_;
	my $head = $self->ra->get_latest_revnum;
	if (!defined $::_revision || $::_revision eq 'BASE:HEAD') {
		return ($base + 1, $head) if (defined $base);
		return (0, $head);
	}
	return ($1, $2) if ($::_revision =~ /^(\d+):(\d+)$/);
	return ($::_revision, $::_revision) if ($::_revision =~ /^\d+$/);
	if ($::_revision =~ /^BASE:(\d+)$/) {
		return ($base + 1, $1) if (defined $base);
		return (0, $head);
	}
	return ($1, $head) if ($::_revision =~ /^(\d+):HEAD$/);
	die "revision argument: $::_revision not understood by git-svn\n",
		"Try using the command-line svn client instead\n";
}

sub tmp_index_do {
	my ($self, $sub) = @_;
	my $old_index = $ENV{GIT_INDEX_FILE};
	$ENV{GIT_INDEX_FILE} = $self->{index};
	my @ret = &$sub;
	if ($old_index) {
		$ENV{GIT_INDEX_FILE} = $old_index;
	} else {
		delete $ENV{GIT_INDEX_FILE};
	}
	wantarray ? @ret : $ret[0];
}

sub assert_index_clean {
	my ($self, $treeish) = @_;

	$self->tmp_index_do(sub {
		command_noisy('read-tree', $treeish) unless -e $self->{index};
		my $x = command_oneline('write-tree');
		my ($y) = (command(qw/cat-file commit/, $treeish) =~
		           /^tree ($::sha1)/mo);
		if ($y ne $x) {
			unlink $self->{index} or croak $!;
			command_noisy('read-tree', $treeish);
		}
		$x = command_oneline('write-tree');
		if ($y ne $x) {
			::fatal "trees ($treeish) $y != $x\n",
			        "Something is seriously wrong...\n";
		}
	});
}

sub get_commit_parents {
	my ($self, $log_entry, @parents) = @_;
	my (%seen, @ret, @tmp);
	# commit parents can be conditionally bound to a particular
	# svn revision via: "svn_revno=commit_sha1", filter them out here:
	foreach my $p (@parents) {
		next unless defined $p;
		if ($p =~ /^(\d+)=($::sha1_short)$/o) {
			push @tmp, $2 if $1 == $log_entry->{revision};
		} else {
			push @tmp, $p if $p =~ /^$::sha1_short$/o;
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

sub full_url {
	my ($self) = @_;
	$self->ra->{url} . (length $self->{path} ? '/' . $self->{path} : '');
}

sub do_git_commit {
	my ($self, $log_entry, @parents) = @_;
	if (my $c = $self->rev_db_get($log_entry->{revision})) {
		croak "$log_entry->{revision} = $c already exists! ",
		      "Why are we refetching it?\n";
	}
	my $author = $log_entry->{author};
	my ($name, $email) = (defined $::users{$author} ? @{$::users{$author}}
	                   : ($author, "$author\@".$self->ra->uuid));
	$ENV{GIT_AUTHOR_NAME} = $ENV{GIT_COMMITTER_NAME} = $name;
	$ENV{GIT_AUTHOR_EMAIL} = $ENV{GIT_COMMITTER_EMAIL} = $email;
	$ENV{GIT_AUTHOR_DATE} = $ENV{GIT_COMMITTER_DATE} = $log_entry->{date};

	my $tree = $log_entry->{tree};
	if (!defined $tree) {
		$tree = $self->tmp_index_do(sub {
		                            command_oneline('write-tree') });
	}
	die "Tree is not a valid sha1: $tree\n" if $tree !~ /^$::sha1$/o;

	my @exec = ('git-commit-tree', $tree);
	foreach ($self->get_commit_parents($log_entry, @parents)) {
		push @exec, '-p', $_;
	}
	defined(my $pid = open3(my $msg_fh, my $out_fh, '>&STDERR', @exec))
	                                                           or croak $!;
	print $msg_fh $log_entry->{log} or croak $!;
	print $msg_fh "\ngit-svn-id: ", $self->full_url, '@',
	              $log_entry->{revision}, ' ',
		      $self->ra->uuid, "\n" or croak $!;
	$msg_fh->flush == 0 or croak $!;
	close $msg_fh or croak $!;
	chomp(my $commit = do { local $/; <$out_fh> });
	close $out_fh or croak $!;
	waitpid $pid, 0;
	croak $? if $?;
	if ($commit !~ /^$::sha1$/o) {
		die "Failed to commit, invalid sha1: $commit\n";
	}

	command_noisy('update-ref',$self->refname, $commit);
	$self->rev_db_set($log_entry->{revision}, $commit);

	$self->{last_rev} = $log_entry->{revision};
	$self->{last_commit} = $commit;
	print "r$log_entry->{revision} = $commit\n";
	return $commit;
}

sub revisions_eq {
	my ($self, $r0, $r1) = @_;
	return 1 if $r0 == $r1;
	my $nr = 0;
	$self->ra->get_log([$self->{path}], $r0, $r1,
	                   0, 0, 1, sub { $nr++ });
	return 0 if ($nr > 1);
	return 1;
}

sub find_parent_branch {
	my ($self, $paths, $rev) = @_;
	return undef unless $::_follow_parent;
	unless (defined $paths) {
		$self->ra->get_log([''], $rev, $rev, 0, 1, 1,
		                   sub { $paths = $_[0] });
	}
	return undef unless defined $paths;

	# look for a parent from another branch:
	my @b_path_components = split m#/#, $self->rel_path;
	my @a_path_components;
	my $i;
	while (@b_path_components) {
		$i = $paths->{'/'.join('/', @b_path_components)};
		last if $i;
		unshift(@a_path_components, pop(@b_path_components));
	}
	goto not_found unless defined $i;
	my $branch_from = $i->copyfrom_path or goto not_found;
	if (@a_path_components) {
		print STDERR "branch_from: $branch_from => ";
		$branch_from .= '/'.join('/', @a_path_components);
		print STDERR $branch_from, "\n";
	}
	my $r = $i->copyfrom_rev;
	my $repos_root = $self->ra->{repos_root};
	my $url = $self->ra->{url};
	my $new_url = $repos_root . $branch_from;
	print STDERR  "Found possible branch point: ",
	              "$new_url => ", $self->full_url, ", $r\n";
	$branch_from =~ s#^/##;
	my $remotes = read_all_remotes();
	my $gs;
	foreach my $repo_id (keys %$remotes) {
		my $u = $remotes->{$repo_id}->{url} or next;
		next if $url ne $u;
		my $fetch = $remotes->{$repo_id}->{fetch};
		foreach my $f (keys %$fetch) {
			next if $f ne $branch_from;
			$gs = Git::SVN->new($fetch->{$f}, $repo_id, $f);
			last;
		}
		last if $gs;
	}
	unless ($gs) {
		my $ref_id = $branch_from;
		$ref_id .= "\@$r" if find_ref($ref_id);
		# just grow a tail if we're not unique enough :x
		$ref_id .= '-' while find_ref($ref_id);
		$gs = Git::SVN->init($new_url, '', $ref_id, $ref_id);
	}
	my ($r0, $parent) = $gs->find_rev_before($r, 1);
	if ($::_follow_parent && (!defined $r0 || !defined $parent)) {
		foreach (1 .. $r) {
			if (my $log_entry = $gs->do_fetch(undef, $_)) {
				$gs->do_git_commit($log_entry);
			}
		}
		($r0, $parent) = $gs->last_rev_commit;
	}
	if (defined $r0 && defined $parent && $gs->revisions_eq($r0, $r)) {
		print STDERR "Found branch parent: ($self->{ref_id}) $parent\n";
		$self->assert_index_clean($parent);
		my $ed;
		if ($self->ra->can_do_switch) {
			print STDERR "Following parent with do_switch\n";
			# do_switch works with svn/trunk >= r22312, but that
			# is not included with SVN 1.4.2 (the latest version
			# at the moment), so we can't rely on it
			$self->{last_commit} = $parent;
			$ed = SVN::Git::Fetcher->new($self);
			$gs->ra->gs_do_switch($r0, $rev, $gs->{path}, 1,
					      $self->full_url, $ed)
			  or die "SVN connection failed somewhere...\n";
		} else {
			print STDERR "Following parent with do_update\n";
			$ed = SVN::Git::Fetcher->new($self);
			$self->ra->gs_do_update($rev, $rev, $self->{path},
			                        1, $ed)
			  or die "SVN connection failed somewhere...\n";
		}
		return $self->make_log_entry($rev, [$parent], $ed);
	}
not_found:
	print STDERR "Branch parent for path: '/",
	             $self->rel_path, "' @ $rev not found\n";
	print STDERR '  ', $_, "\n" foreach (sort keys %$paths);
	return undef;
}

sub do_fetch {
	my ($self, $paths, $rev) = @_;
	my $ed;
	my ($last_rev, @parents);
	if ($self->{last_commit}) {
		$ed = SVN::Git::Fetcher->new($self);
		$last_rev = $self->{last_rev};
		$ed->{c} = $self->{last_commit};
		@parents = ($self->{last_commit});
	} else {
		$last_rev = $rev;
		if (my $log_entry = $self->find_parent_branch($paths, $rev)) {
			return $log_entry;
		}
		$ed = SVN::Git::Fetcher->new($self);
	}
	unless ($self->ra->gs_do_update($last_rev, $rev,
	                                $self->{path}, 1, $ed)) {
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

	return undef if ($ed->{nr} == 0 && scalar @$untracked == 0);

	open my $un, '>>', "$self->{dir}/unhandled.log" or croak $!;
	print $un "r$rev\n" or croak $!;
	print $un $_, "\n" foreach @$untracked;
	my %log_entry = ( parents => $parents || [], revision => $rev,
	                  log => '');
	my $rp = $self->ra->rev_proplist($rev);
	foreach (sort keys %$rp) {
		my $v = $rp->{$_};
		if (/^svn:(author|date|log)$/) {
			$log_entry{$1} = $v;
		} else {
			print $un "  rev_prop: ", uri_encode($_), ' ',
		                  uri_encode($v), "\n";
		}
	}
	close $un or croak $!;

	$log_entry{date} = parse_svn_date($log_entry{date});
	$log_entry{author} = check_author($log_entry{author});
	$log_entry{log} .= "\n";
	\%log_entry;
}

sub fetch {
	my ($self, @parents) = @_;
	my ($last_rev, $last_commit) = $self->last_rev_commit;
	my ($base, $head) = $self->parse_revision($last_rev);
	return if ($base > $head);
	if (defined $last_commit) {
		$self->assert_index_clean($last_commit);
	}
	my $inc = 1000;
	my ($min, $max) = ($base, $head < $base + $inc ? $head : $base + $inc);
	my $err_handler = $SVN::Error::handler;
	my $err;
	$SVN::Error::handler = sub { ($err) = @_; skip_unknown_revs($err); } ;
	while (1) {
		my @revs;
		$self->ra->get_log([$self->{path}], $min, $max, 0, 1, 1, sub {
			my ($paths, $rev, $author, $date, $log) = @_;
			push @revs, [ $paths, $rev ] });
		if (! @revs && $err) {
			print STDERR "Branch probably deleted:\n  ",
			             $err->expanded_message,
				     "\nWill attempt to follow revisions ",
				     "committed before the deletion\n";
			@revs = map { [ undef, $_ ] } ($min .. $max);
		}
		foreach (@revs) {
			if (my $log_entry = $self->do_fetch(@$_)) {
				$self->do_git_commit($log_entry, @parents);
			}
		}
		last if $max >= $head || $err;
		$min = $max + 1;
		$max += $inc;
		$max = $head if ($max > $head);
	}
	$SVN::Error::handler = $err_handler;
}

sub set_tree_cb {
	my ($self, $log_entry, $tree, $rev, $date, $author) = @_;
	# TODO: enable and test optimized commits:
	if (0 && $rev == ($self->{last_rev} + 1)) {
		$log_entry->{revision} = $rev;
		$log_entry->{author} = $author;
		$self->do_git_commit($log_entry, "$rev=$tree");
	} else {
		$self->fetch("$rev=$tree");
	}
}

sub set_tree {
	my ($self, $tree) = (shift, shift);
	my $log_entry = ::get_commit_entry($tree);
	unless ($self->{last_rev}) {
		fatal("Must have an existing revision to commit\n");
	}
	my $pool = SVN::Pool->new;
	my $ed = SVN::Git::Editor->new({ r => $self->{last_rev},
	                                 ra => $self->ra->dup,
	                                 svn_path => $self->ra->{svn_path}
	                               },
	                               $self->ra->get_commit_editor(
	                                 $log_entry->{log}, sub {
	                                   $self->set_tree_cb($log_entry,
					                      $tree, @_);
	                               }),
	                               $pool);
	my $mods = $ed->apply_diff($self->{last_commit}, $tree);
	if (@$mods == 0) {
		print "No changes\nr$self->{last_rev} = $tree\n";
	}
	$pool->clear;
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
		return;
	}
	croak "Error from SVN, ($errno): ", $err->expanded_message,"\n";
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

sub rev_db_set {
	my ($self, $rev, $commit) = @_;
	length $commit == 40 or croak "arg3 must be a full SHA1 hexsum\n";
	open my $fh, '+<', $self->{db_path} or croak $!;
	my $offset = $rev * 41;
	# assume that append is the common case:
	seek $fh, 0, 2 or croak $!;
	my $pos = tell $fh;
	if ($pos < $offset) {
		print $fh (('0' x 40),"\n") x (($offset - $pos) / 41)
		  or croak $!;
	}
	seek $fh, $offset, 0 or croak $!;
	print $fh $commit,"\n" or croak $!;
	close $fh or croak $!;
}

sub rev_db_get {
	my ($self, $rev) = @_;
	my $ret;
	my $offset = $rev * 41;
	open my $fh, '<', $self->{db_path} or croak $!;
	if (seek $fh, $offset, 0) {
		$ret = readline $fh;
		if (defined $ret) {
			chomp $ret;
			$ret = undef if ($ret =~ /^0{40}$/);
		}
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
	mkpath([$dir]);
	unless (-f "$dir/.rev_db") {
		open my $fh, '>>', "$dir/.rev_db" or croak $!;
		close $fh or croak $!;
	}
	bless { ref_id => $ref_id, dir => $dir, index => "$dir/index",
	        path => $path,
	        db_path => "$dir/.rev_db", repo_id => $repo_id }, $class;
}

sub uri_encode {
	my ($f) = @_;
	$f =~ s#([^a-zA-Z0-9\*!\:_\./\-])#uc sprintf("%%%02x",ord($1))#eg;
	$f
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
	require Digest::MD5;
	$self;
}

sub set_path_strip {
	my ($self, $path) = @_;
	$self->{path_strip} = qr/^\Q$path\E\/?/;
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
	$path =~ s!$self->{path_strip}!! if $self->{path_strip};
	$path;
}

sub delete_entry {
	my ($self, $path, $rev, $pb) = @_;

	my $gpath = $self->git_path($path);
	# remove entire directories.
	if (command('ls-tree', $self->{c}, '--', $gpath) =~ /^040000 tree/) {
		my ($ls, $ctx) = command_output_pipe(qw/ls-tree
		                                     -r --name-only -z/,
				                     $self->{c}, '--', $gpath);
		local $/ = "\0";
		while (<$ls>) {
			chomp;
			$self->{gii}->remove($_);
			print "\tD\t$_\n" unless $self->{q};
		}
		print "\tD\t$gpath/\n" unless $self->{q};
		command_close_pipe($ls, $ctx);
		$self->{empty}->{$path} = 0
	} else {
		$self->{gii}->remove($gpath);
		print "\tD\t$gpath\n" unless $self->{q};
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
	$self->{gii}->update($fb->{mode_b}, $hash, $path) or croak $!;
	print "\t$fb->{action}\t$path\n" if $fb->{action} && ! $self->{q};
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
	foreach (qw/svn_path r ra/) {
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
	my ($self, $tree_b) = @_;
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
	                           qw/ls-tree --name-only -r -z/, $tree_b);
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
	my $t = $self->{ra}->check_path($full_path, $self->{r});
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

# this drives the editor
sub apply_diff {
	my ($self, $tree_a, $tree_b) = @_;
	my @diff_tree = qw(diff-tree -z -r);
	if ($::_cp_similarity) {
		push @diff_tree, "-C$::_cp_similarity";
	} else {
		push @diff_tree, '-C';
	}
	push @diff_tree, '--find-copies-harder' if $::_find_copies_harder;
	push @diff_tree, "-l$::_l" if defined $::_l;
	push @diff_tree, $tree_a, $tree_b;
	my ($diff_fh, $ctx) = command_output_pipe(@diff_tree);
	my $nl = $/;
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
	$/ = $nl;

	my %o = ( D => 1, R => 0, C => -1, A => 3, M => 3, T => 3 );
	foreach my $m (sort { $o{$a->{chg}} <=> $o{$b->{chg}} } @mods) {
		my $f = $m->{chg};
		if (defined $o{$f}) {
			$self->$f($m);
		} else {
			fatal("Invalid change type: $f\n");
		}
	}
	$self->rmdirs($tree_b) if $::_rmdir;
	if (@mods == 0) {
		$self->abort_edit;
	} else {
		$self->close_edit;
	}
	\@mods;
}

package Git::SVN::Ra;
use vars qw/@ISA $config_dir/;
use strict;
use warnings;
my ($can_do_switch);
my %RA;

BEGIN {
	# enforce temporary pool usage for some simple functions
	my $e;
	foreach (qw/get_latest_revnum rev_proplist get_file
	            check_path get_dir get_uuid get_repos_root/) {
		$e .= "sub $_ {
			my \$self = shift;
			my \$pool = SVN::Pool->new;
			my \@ret = \$self->SUPER::$_(\@_,\$pool);
			\$pool->clear;
			wantarray ? \@ret : \$ret[0]; }\n";
	}
	eval $e;
}

sub new {
	my ($class, $url) = @_;
	$url =~ s!/+$!!;
	return $RA{$url} if $RA{$url};

	SVN::_Core::svn_config_ensure($config_dir, undef);
	my ($baton, $callbacks) = SVN::Core::auth_open_helper([
	    SVN::Client::get_simple_provider(),
	    SVN::Client::get_ssl_server_trust_file_provider(),
	    SVN::Client::get_simple_prompt_provider(
	      \&Git::SVN::Prompt::simple, 2),
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
	$self->{svn_path} =~ s#^\Q$self->{repos_root}\E/*##;
	$RA{$url} = bless $self, $class;
}

sub DESTROY {
	# do not call the real DESTROY since we store ourselves in %RA
}

sub dup {
	my ($self) = @_;
	my $dup = SVN::Ra->new(pool => SVN::Pool->new,
				map { $_ => $self->{$_} } qw/config url
	             auth auth_provider_callbacks repos_root svn_path/);
	bless $dup, ref $self;
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

sub uuid {
	my ($self) = @_;
	$self->{uuid} ||= $self->get_uuid;
}

sub gs_do_update {
	my ($self, $rev_a, $rev_b, $path, $recurse, $editor) = @_;
	my $pool = SVN::Pool->new;
	$editor->set_path_strip($path);
	my $reporter = $self->do_update($rev_b, $path, $recurse,
	                                $editor, $pool);
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef) : ();
	my $new = ($rev_a == $rev_b);
	$reporter->set_path('', $rev_a, $new, @lock, $pool);
	$reporter->finish_report($pool);
	$pool->clear;
	$editor->{git_commit_ok};
}

sub gs_do_switch {
	my ($self, $rev_a, $rev_b, $path, $recurse, $url_b, $editor) = @_;
	my $pool = SVN::Pool->new;
	$editor->set_path_strip($path);
	my $reporter = $self->do_switch($rev_b, $path, $recurse,
	                                $url_b, $editor, $pool);
	my @lock = $SVN::Core::VERSION ge '1.2.0' ? (undef) : ();
	$reporter->set_path('', $rev_a, 0, @lock, $pool);
	$reporter->finish_report($pool);
	$pool->clear;
	$editor->{git_commit_ok};
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
	if ($c->{l} && $c->{l}->[-1] eq "...\n" &&
				$c->{a_raw} =~ /\@([a-f\d\-]+)>$/) {
		my @log = command(qw/cat-file commit/, $c->{c});
		shift @log while ($log[0] ne "\n");
		shift @log;
		@{$c->{l}} = grep !/^git-svn-id: /, @log;

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
	my ($r_min, $r_max) = @_;
	my $gs = Git::SVN->_new;
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
		$t += ::tz_to_s_offset($tz);
	} elsif ($tz =~ s/^\-//) {
		$t -= ::tz_to_s_offset($tz);
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
	foreach my $x (qw/raw diff/) {
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
	@args = (git_svn_log_cmd($r_min, $r_max), @args);
	my $log = command_output_pipe(@args);
	run_pager();
	my (@k, $c, $d);
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
		Git::SVN->init($l_map{$ref_id}, '', $ref_id, $ref_id);
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
		$root_path =~ s#^\Q$ra->{repos_root}\E/*##;
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

__END__

Data structures:

$log_entry hashref as returned by libsvn_log_entry()
{
	log => 'whitespace-formatted log entry
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
