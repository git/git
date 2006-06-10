#!/usr/bin/env perl
# Copyright (C) 2006, Eric Wong <normalperson@yhbt.net>
# License: GPL v2 or later
use warnings;
use strict;
use vars qw/	$AUTHOR $VERSION
		$SVN_URL $SVN_INFO $SVN_WC $SVN_UUID
		$GIT_SVN_INDEX $GIT_SVN
		$GIT_DIR $REV_DIR/;
$AUTHOR = 'Eric Wong <normalperson@yhbt.net>';
$VERSION = '1.1.0-pre';

use Cwd qw/abs_path/;
$GIT_DIR = abs_path($ENV{GIT_DIR} || '.git');
$ENV{GIT_DIR} = $GIT_DIR;

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
use Getopt::Long qw/:config gnu_getopt no_ignore_case auto_abbrev/;
use File::Spec qw//;
use POSIX qw/strftime/;
my $sha1 = qr/[a-f\d]{40}/;
my $sha1_short = qr/[a-f\d]{4,40}/;
my ($_revision,$_stdin,$_no_ignore_ext,$_no_stop_copy,$_help,$_rmdir,$_edit,
	$_find_copies_harder, $_l, $_version, $_upgrade, $_authors);
my (@_branch_from, %tree_map, %users);
my ($_svn_co_url_revs, $_svn_pg_peg_revs);

my %fc_opts = ( 'no-ignore-externals' => \$_no_ignore_ext,
		'branch|b=s' => \@_branch_from,
		'authors-file|A=s' => \$_authors );

# yes, 'native' sets "\n".  Patches to fix this for non-*nix systems welcome:
my %EOL = ( CR => "\015", LF => "\012", CRLF => "\015\012", native => "\012" );

my %cmd = (
	fetch => [ \&fetch, "Download new revisions from SVN",
			{ 'revision|r=s' => \$_revision, %fc_opts } ],
	init => [ \&init, "Initialize a repo for tracking" .
			  " (requires URL argument)", { } ],
	commit => [ \&commit, "Commit git revisions to SVN",
			{	'stdin|' => \$_stdin,
				'edit|e' => \$_edit,
				'rmdir' => \$_rmdir,
				'find-copies-harder' => \$_find_copies_harder,
				'l=i' => \$_l,
				%fc_opts,
			} ],
	'show-ignore' => [ \&show_ignore, "Show svn:ignore listings", { } ],
	rebuild => [ \&rebuild, "Rebuild git-svn metadata (after git clone)",
			{ 'no-ignore-externals' => \$_no_ignore_ext,
			  'upgrade' => \$_upgrade } ],
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

# convert GetOpt::Long specs for use by git-repo-config
foreach my $o (keys %opts) {
	my $v = $opts{$o};
	my ($key) = ($o =~ /^([a-z\-]+)/);
	$key =~ s/-//g;
	my $arg = 'git-repo-config';
	$arg .= ' --int' if ($o =~ /=i$/);
	$arg .= ' --bool' if ($o !~ /=[sfi]$/);
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

GetOptions(%opts, 'help|H|h' => \$_help,
		'version|V' => \$_version,
		'id|i=s' => \$GIT_SVN) or exit 1;

$GIT_SVN ||= $ENV{GIT_SVN_ID} || 'git-svn';
$GIT_SVN_INDEX = "$GIT_DIR/$GIT_SVN/index";
$SVN_URL = undef;
$REV_DIR = "$GIT_DIR/$GIT_SVN/revs";
$SVN_WC = "$GIT_DIR/$GIT_SVN/tree";

usage(0) if $_help;
version() if $_version;
usage(1) unless defined $cmd;
load_authors() if $_authors;
svn_compat_check();
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
			my $x = s#=s$## ? '<arg>' : s#=i$## ? '<num>' : '';
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
		my $id = $commit[$#commit];
		my ($url, $rev, $uuid) = ($id =~ /^git-svn-id:\s(\S+?)\@(\d+)
						\s([a-f\d\-]+)$/x);
		if (!$rev || !$uuid || !$url) {
			# some of the original repositories I made had
			# indentifiers like this:
			($rev, $uuid) = ($id =~/^git-svn-id:\s(\d+)
							\@([a-f\d\-]+)/x);
			if (!$rev || !$uuid) {
				croak "Unable to extract revision or UUID from ",
					"$c, $id\n";
			}
		}

		# if we merged or otherwise started elsewhere, this is
		# how we break out of it
		next if (defined $SVN_UUID && ($uuid ne $SVN_UUID));
		next if (defined $SVN_URL && defined $url && ($url ne $SVN_URL));

		print "r$rev = $c\n";
		unless (defined $latest) {
			if (!$SVN_URL && !$url) {
				croak "SVN repository location required: $url\n";
			}
			$SVN_URL ||= $url;
			$SVN_UUID ||= $uuid;
			setup_git_svn();
			$latest = $rev;
		}
		assert_revision_eq_or_unknown($rev, $c);
		sys('git-update-ref',"$GIT_SVN/revs/$rev",$c);
		$newest_rev = $rev if ($rev > $newest_rev);
	}
	close $rev_list or croak $?;
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
		exec('git-write-tree');
	}
	waitpid $pid, 0;

	if ($_upgrade) {
		print STDERR <<"";
Keeping deprecated refs/head/$GIT_SVN-HEAD for now.  Please remove it
when you have upgraded your tools and habits to use refs/remotes/$GIT_SVN

	}
}

sub init {
	$SVN_URL = shift or die "SVN repository location required " .
				"as a command-line argument\n";
	unless (-d $GIT_DIR) {
		sys('git-init-db');
	}
	setup_git_svn();
}

sub fetch {
	my (@parents) = @_;
	check_upgrade_needed();
	$SVN_URL ||= file_to_s("$GIT_DIR/$GIT_SVN/info/url");
	my @log_args = -d $SVN_WC ? ($SVN_WC) : ($SVN_URL);
	unless ($_revision) {
		$_revision = -d $SVN_WC ? 'BASE:HEAD' : '0:HEAD';
	}
	push @log_args, "-r$_revision";
	push @log_args, '--stop-on-copy' unless $_no_stop_copy;

	my $svn_log = svn_log_raw(@log_args);

	my $base = next_log_entry($svn_log) or croak "No base revision!\n";
	my $last_commit = undef;
	unless (-d $SVN_WC) {
		svn_cmd_checkout($SVN_URL,$base->{revision},$SVN_WC);
		chdir $SVN_WC or croak $!;
		read_uuid();
		$last_commit = git_commit($base, @parents);
		assert_tree($last_commit);
	} else {
		chdir $SVN_WC or croak $!;
		read_uuid();
		$last_commit = file_to_s("$REV_DIR/$base->{revision}");
	}
	my @svn_up = qw(svn up);
	push @svn_up, '--ignore-externals' unless $_no_ignore_ext;
	my $last = $base;
	while (my $log_msg = next_log_entry($svn_log)) {
		assert_tree($last_commit);
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
	unless (-e "$GIT_DIR/refs/heads/master") {
		sys(qw(git-update-ref refs/heads/master),$last_commit);
	}
	return $last;
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

	fetch();
	chdir $SVN_WC or croak $!;
	my $info = svn_info('.');
	read_uuid($info);
	my $svn_current_rev =  $info->{'Last Changed Rev'};
	foreach my $c (@revs) {
		my $mods = svn_checkout_tree($svn_current_rev, $c);
		if (scalar @$mods == 0) {
			print "Skipping, no changes detected\n";
			next;
		}
		$svn_current_rev = svn_commit_tree($svn_current_rev, $c);
	}
	print "Done committing ",scalar @revs," revisions to SVN\n";
}

sub show_ignore {
	require File::Find or die $!;
	my $exclude_file = "$GIT_DIR/info/exclude";
	open my $fh, '<', $exclude_file or croak $!;
	chomp(my @excludes = (<$fh>));
	close $fh or croak $!;

	$SVN_URL ||= file_to_s("$GIT_DIR/$GIT_SVN/info/url");
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

########################### utility functions #########################

sub read_uuid {
	return if $SVN_UUID;
	my $info = shift || svn_info('.');
	$SVN_UUID = $info->{'Repository UUID'} or
					croak "Repository UUID unreadable\n";
	s_to_file($SVN_UUID,"$GIT_DIR/$GIT_SVN/info/uuid");
}

sub setup_git_svn {
	defined $SVN_URL or croak "SVN repository location required\n";
	unless (-d $GIT_DIR) {
		croak "GIT_DIR=$GIT_DIR does not exist!\n";
	}
	mkpath(["$GIT_DIR/$GIT_SVN"]);
	mkpath(["$GIT_DIR/$GIT_SVN/info"]);
	mkpath([$REV_DIR]);
	s_to_file($SVN_URL,"$GIT_DIR/$GIT_SVN/info/url");

	open my $fd, '>>', "$GIT_DIR/$GIT_SVN/info/exclude" or croak $!;
	print $fd '.svn',"\n";
	close $fd or croak $!;
}

sub assert_svn_wc_clean {
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

sub assert_tree {
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

	my $old_index = $ENV{GIT_INDEX_FILE};
	my $tmpindex = $GIT_SVN_INDEX.'.assert-tmp';
	if (-e $tmpindex) {
		unlink $tmpindex or croak $!;
	}
	$ENV{GIT_INDEX_FILE} = $tmpindex;
	index_changes(1);
	chomp(my $tree = `git-write-tree`);
	if ($old_index) {
		$ENV{GIT_INDEX_FILE} = $old_index;
	} else {
		delete $ENV{GIT_INDEX_FILE};
	}
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

sub svn_checkout_tree {
	my ($svn_rev, $treeish) = @_;
	my $from = file_to_s("$REV_DIR/$svn_rev");
	assert_tree($from);
	print "diff-tree $from $treeish\n";
	my $pid = open my $diff_fh, '-|';
	defined $pid or croak $!;
	if ($pid == 0) {
		my @diff_tree = qw(git-diff-tree -z -r -C);
		push @diff_tree, '--find-copies-harder' if $_find_copies_harder;
		push @diff_tree, "-l$_l" if defined $_l;
		exec(@diff_tree, $from, $treeish) or croak $!;
	}
	my $mods = parse_diff_tree($diff_fh);
	unless (@$mods) {
		# git can do empty commits, but SVN doesn't allow it...
		return $mods;
	}
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

sub svn_commit_tree {
	my ($svn_rev, $commit) = @_;
	my $commit_msg = "$GIT_DIR/$GIT_SVN/.svn-commit.tmp.$$";
	my %log_msg = ( msg => '' );
	open my $msg, '>', $commit_msg or croak $!;

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

	my ($oneline) = ($log_msg{msg} =~ /([^\n\r]+)/);
	print "Committing $commit: $oneline\n";

	my @ci_output = safe_qx(qw(svn commit -F),$commit_msg);
	my ($committed) = grep(/^Committed revision \d+\./,@ci_output);
	unlink $commit_msg;
	defined $committed or croak
			"Commit output failed to parse committed revision!\n",
			join("\n",@ci_output),"\n";
	my ($rev_committed) = ($committed =~ /^Committed revision (\d+)\./);

	my @svn_up = qw(svn up);
	push @svn_up, '--ignore-externals' unless $_no_ignore_ext;
	if ($rev_committed == ($svn_rev + 1)) {
		push @svn_up, "-r$rev_committed";
		sys(@svn_up);
		my $info = svn_info('.');
		my $date = $info->{'Last Changed Date'} or die "Missing date\n";
		if ($info->{'Last Changed Rev'} != $rev_committed) {
			croak "$info->{'Last Changed Rev'} != $rev_committed\n"
		}
		my ($Y,$m,$d,$H,$M,$S,$tz) = ($date =~
					/(\d{4})\-(\d\d)\-(\d\d)\s
					 (\d\d)\:(\d\d)\:(\d\d)\s([\-\+]\d+)/x)
					 or croak "Failed to parse date: $date\n";
		$log_msg{date} = "$tz $Y-$m-$d $H:$M:$S";
		$log_msg{author} = $info->{'Last Changed Author'};
		$log_msg{revision} = $rev_committed;
		$log_msg{msg} .= "\n";
		my $parent = file_to_s("$REV_DIR/$svn_rev");
		git_commit(\%log_msg, $parent, $commit);
		return $rev_committed;
	}
	# resync immediately
	push @svn_up, "-r$svn_rev";
	sys(@svn_up);
	return fetch("$rev_committed=$commit")->{revision};
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
	croak if $?;
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

	my $eol = $EOL{$es} or undef;
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
	my $no_text_base = shift;
	do_update_index([qw/git-diff-files --name-only -z/],
			'remove',
			$no_text_base);
	do_update_index([qw/git-ls-files -z --others/,
			      "--exclude-from=$GIT_DIR/$GIT_SVN/info/exclude"],
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
	my $revno = shift;
	if (-f "$REV_DIR/$revno") {
		croak "$REV_DIR/$revno already exists! ",
				"Why are we refetching it?";
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

sub assert_revision_eq_or_unknown {
	my ($revno, $commit) = @_;
	if (-f "$REV_DIR/$revno") {
		my $current = file_to_s("$REV_DIR/$revno");
		if (($commit ne $current) && !trees_eq($commit, $current)) {
			croak "$REV_DIR/$revno already exists!\n",
				"current: $current\nexpected: $commit\n";
		}
		return;
	}
}

sub git_commit {
	my ($log_msg, @parents) = @_;
	assert_revision_unknown($log_msg->{revision});
	my $out_fh = IO::File->new_tmpfile or croak $!;

	map_tree_joins() if (@_branch_from && !%tree_map);

	# commit parents can be conditionally bound to a particular
	# svn revision via: "svn_revno=commit_sha1", filter them out here:
	my @exec_parents;
	foreach my $p (@parents) {
		next unless defined $p;
		if ($p =~ /^(\d+)=($sha1_short)$/o) {
			if ($1 == $log_msg->{revision}) {
				push @exec_parents, $2;
			}
		} else {
			push @exec_parents, $p if $p =~ /$sha1_short/o;
		}
	}

	my $pid = fork;
	defined $pid or croak $!;
	if ($pid == 0) {
		$ENV{GIT_INDEX_FILE} = $GIT_SVN_INDEX;
		index_changes();
		chomp(my $tree = `git-write-tree`);
		croak if $?;
		if (exists $tree_map{$tree}) {
			my %seen_parent = map { $_ => 1 } @exec_parents;
			foreach (@{$tree_map{$tree}}) {
				# MAXPARENT is defined to 16 in commit-tree.c:
				if ($seen_parent{$_} || @exec_parents > 16) {
					next;
				}
				push @exec_parents, $_;
				$seen_parent{$_} = 1;
			}
		}
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
		open STDOUT, '>&', $out_fh or croak $!;
		exec @exec or croak $!;
	}
	waitpid($pid,0);
	croak if $?;

	$out_fh->flush == 0 or croak $!;
	seek $out_fh, 0, 0 or croak $!;
	chomp(my $commit = do { local $/; <$out_fh> });
	if ($commit !~ /^$sha1$/o) {
		croak "Failed to commit, invalid sha1: $commit\n";
	}
	my @update_ref = ('git-update-ref',"refs/remotes/$GIT_SVN",$commit);
	if (my $primary_parent = shift @exec_parents) {
		$pid = fork;
		defined $pid or croak $!;
		if (!$pid) {
			close STDERR;
			close STDOUT;
			exec 'git-rev-parse','--verify',
						"refs/remotes/$GIT_SVN^0";
		}
		waitpid $pid, 0;
		push @update_ref, $primary_parent unless $?;
	}
	sys(@update_ref);
	sys('git-update-ref',"$GIT_SVN/revs/$log_msg->{revision}",$commit);
	print "r$log_msg->{revision} = $commit\n";
	return $commit;
}

sub set_commit_env {
	my ($log_msg) = @_;
	my $author = $log_msg->{author};
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
		exec('git-cat-file','blob',$blob);
	}
	waitpid $pid, 0;
	croak $? if $?;

	close $blob_fh or croak $!;
}

sub safe_qx {
	my $pid = open my $child, '-|';
	defined $pid or croak $!;
	if ($pid == 0) {
		exec(@_) or croak $?;
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
			exec('git-rev-parse',"$GIT_SVN-HEAD") or croak $?;
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
	foreach my $br (@_branch_from) {
		my $pid = open my $pipe, '-|';
		defined $pid or croak $!;
		if ($pid == 0) {
			exec(qw(git-rev-list --pretty=raw), $br) or croak $?;
		}
		while (<$pipe>) {
			if (/^commit ($sha1)$/o) {
				my $commit = $1;
				my ($tree) = (<$pipe> =~ /^tree ($sha1)$/o);
				unless (defined $tree) {
					die "Failed to parse commit $commit\n";
				}
				push @{$tree_map{$tree}}, $commit;
			}
		}
		close $pipe or croak $?;
	}
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

sub svn_propget_base {
	my ($p, $f) = @_;
	$f .= '@BASE' if $_svn_pg_peg_revs;
	return safe_qx(qw/svn propget/, $p, $f);
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
