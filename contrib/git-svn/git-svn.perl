#!/usr/bin/env perl
# Copyright (C) 2006, Eric Wong <normalperson@yhbt.net>
# License: GPL v2 or later
use warnings;
use strict;
use vars qw/	$AUTHOR $VERSION
		$SVN_URL $SVN_INFO $SVN_WC
		$GIT_SVN_INDEX $GIT_SVN
		$GIT_DIR $REV_DIR/;
$AUTHOR = 'Eric Wong <normalperson@yhbt.net>';
$VERSION = '0.9.1';
$GIT_DIR = $ENV{GIT_DIR} || "$ENV{PWD}/.git";
$GIT_SVN = $ENV{GIT_SVN_ID} || 'git-svn';
$GIT_SVN_INDEX = "$GIT_DIR/$GIT_SVN/index";
$ENV{GIT_DIR} ||= $GIT_DIR;
$SVN_URL = undef;
$REV_DIR = "$GIT_DIR/$GIT_SVN/revs";
$SVN_WC = "$GIT_DIR/$GIT_SVN/tree";

# make sure the svn binary gives consistent output between locales and TZs:
$ENV{TZ} = 'UTC';
$ENV{LC_ALL} = 'C';

# If SVN:: library support is added, please make the dependencies
# optional and preserve the capability to use the command-line client.
# use eval { require SVN::... } to make it lazy load
use Carp qw/croak/;
use IO::File qw//;
use File::Basename qw/dirname basename/;
use File::Path qw/mkpath/;
use Getopt::Long qw/:config gnu_getopt no_ignore_case auto_abbrev/;
use File::Spec qw//;
my $sha1 = qr/[a-f\d]{40}/;
my $sha1_short = qr/[a-f\d]{6,40}/;
my ($_revision,$_stdin,$_no_ignore_ext,$_no_stop_copy,$_help,$_rmdir,$_edit,
	$_find_copies_harder, $_l, $_version);

GetOptions(	'revision|r=s' => \$_revision,
		'no-ignore-externals' => \$_no_ignore_ext,
		'stdin|' => \$_stdin,
		'edit|e' => \$_edit,
		'rmdir' => \$_rmdir,
		'help|H|h' => \$_help,
		'find-copies-harder' => \$_find_copies_harder,
		'l=i' => \$_l,
		'version|V' => \$_version,
		'no-stop-on-copy' => \$_no_stop_copy );
my %cmd = (
	fetch => [ \&fetch, "Download new revisions from SVN" ],
	init => [ \&init, "Initialize and fetch (import)"],
	commit => [ \&commit, "Commit git revisions to SVN" ],
	'show-ignore' => [ \&show_ignore, "Show svn:ignore listings" ],
	rebuild => [ \&rebuild, "Rebuild git-svn metadata (after git clone)" ],
	help => [ \&usage, "Show help" ],
);
my $cmd;
for (my $i = 0; $i < @ARGV; $i++) {
	if (defined $cmd{$ARGV[$i]}) {
		$cmd = $ARGV[$i];
		splice @ARGV, $i, 1;
		last;
	}
};

# we may be called as git-svn-(command), or git-svn(command).
foreach (keys %cmd) {
	if (/git\-svn\-?($_)(?:\.\w+)?$/) {
		$cmd = $1;
		last;
	}
}
usage(0) if $_help;
version() if $_version;
usage(1) unless (defined $cmd);
svn_check_ignore_externals();
$cmd{$cmd}->[0]->(@ARGV);
exit 0;

####################### primary functions ######################
sub usage {
	my $exit = shift || 0;
	my $fd = $exit ? \*STDERR : \*STDOUT;
	print $fd <<"";
git-svn - bidirectional operations between a single Subversion tree and git
Usage: $0 <command> [options] [arguments]\n
Available commands:

	foreach (sort keys %cmd) {
		print $fd '  ',pack('A10',$_),$cmd{$_}->[1],"\n";
	}
	print $fd <<"";
\nGIT_SVN_ID may be set in the environment to an arbitrary identifier if
you're tracking multiple SVN branches/repositories in one git repository
and want to keep them separate.

	exit $exit;
}

sub version {
	print "git-svn version $VERSION\n";
	exit 0;
}

sub rebuild {
	$SVN_URL = shift or undef;
	my $repo_uuid;
	my $newest_rev = 0;

	my $pid = open(my $rev_list,'-|');
	defined $pid or croak $!;
	if ($pid == 0) {
		exec("git-rev-list","$GIT_SVN-HEAD") or croak $!;
	}
	my $first;
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
		print "r$rev = $c\n";
		unless (defined $first) {
			if (!$SVN_URL && !$url) {
				croak "SVN repository location required: $url\n";
			}
			$SVN_URL ||= $url;
			$repo_uuid = setup_git_svn();
			$first = $rev;
		}
		if ($uuid ne $repo_uuid) {
			croak "Repository UUIDs do not match!\ngot: $uuid\n",
						"expected: $repo_uuid\n";
		}
		assert_revision_eq_or_unknown($rev, $c);
		sys('git-update-ref',"$GIT_SVN/revs/$rev",$c);
		$newest_rev = $rev if ($rev > $newest_rev);
	}
	close $rev_list or croak $?;
	if (!chdir $SVN_WC) {
		my @svn_co = ('svn','co',"-r$first");
		push @svn_co, '--ignore-externals' unless $_no_ignore_ext;
		sys(@svn_co, $SVN_URL, $SVN_WC);
		chdir $SVN_WC or croak $!;
	}

	$pid = fork;
	defined $pid or croak $!;
	if ($pid == 0) {
		my @svn_up = qw(svn up);
		push @svn_up, '--ignore-externals' unless $_no_ignore_ext;
		sys(@svn_up,"-r$newest_rev");
		$ENV{GIT_INDEX_FILE} = $GIT_SVN_INDEX;
		git_addremove();
		exec('git-write-tree');
	}
	waitpid $pid, 0;
}

sub init {
	$SVN_URL = shift or croak "SVN repository location required\n";
	unless (-d $GIT_DIR) {
		sys('git-init-db');
	}
	setup_git_svn();
}

sub fetch {
	my (@parents) = @_;
	$SVN_URL ||= file_to_s("$GIT_DIR/$GIT_SVN/info/url");
	my @log_args = -d $SVN_WC ? ($SVN_WC) : ($SVN_URL);
	unless ($_revision) {
		$_revision = -d $SVN_WC ? 'BASE:HEAD' : '0:HEAD';
	}
	push @log_args, "-r$_revision";
	push @log_args, '--stop-on-copy' unless $_no_stop_copy;

	my $svn_log = svn_log_raw(@log_args);
	@$svn_log = sort { $a->{revision} <=> $b->{revision} } @$svn_log;

	my $base = shift @$svn_log or croak "No base revision!\n";
	my $last_commit = undef;
	unless (-d $SVN_WC) {
		my @svn_co = ('svn','co',"-r$base->{revision}");
		push @svn_co,'--ignore-externals' unless $_no_ignore_ext;
		sys(@svn_co, $SVN_URL, $SVN_WC);
		chdir $SVN_WC or croak $!;
		$last_commit = git_commit($base, @parents);
		unless (-f "$GIT_DIR/refs/heads/master") {
			sys(qw(git-update-ref refs/heads/master),$last_commit);
		}
		assert_svn_wc_clean($base->{revision}, $last_commit);
	} else {
		chdir $SVN_WC or croak $!;
		$last_commit = file_to_s("$REV_DIR/$base->{revision}");
	}
	my @svn_up = qw(svn up);
	push @svn_up, '--ignore-externals' unless $_no_ignore_ext;
	my $last_rev = $base->{revision};
	foreach my $log_msg (@$svn_log) {
		assert_svn_wc_clean($last_rev, $last_commit);
		$last_rev = $log_msg->{revision};
		sys(@svn_up,"-r$last_rev");
		$last_commit = git_commit($log_msg, $last_commit, @parents);
	}
	assert_svn_wc_clean($last_rev, $last_commit);
	return pop @$svn_log;
}

sub commit {
	my (@commits) = @_;
	if ($_stdin || !@commits) {
		print "Reading from stdin...\n";
		@commits = ();
		while (<STDIN>) {
			if (/\b([a-f\d]{6,40})\b/) {
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
	my $svn_current_rev =  svn_info('.')->{'Last Changed Rev'};
	foreach my $c (@revs) {
		print "Committing $c\n";
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
		@{$ign{$_}} = safe_qx(qw(svn propget svn:ignore),$_);
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

sub setup_git_svn {
	defined $SVN_URL or croak "SVN repository location required\n";
	unless (-d $GIT_DIR) {
		croak "GIT_DIR=$GIT_DIR does not exist!\n";
	}
	mkpath(["$GIT_DIR/$GIT_SVN"]);
	mkpath(["$GIT_DIR/$GIT_SVN/info"]);
	mkpath([$REV_DIR]);
	s_to_file($SVN_URL,"$GIT_DIR/$GIT_SVN/info/url");
	my $uuid = svn_info($SVN_URL)->{'Repository UUID'} or
					croak "Repository UUID unreadable\n";
	s_to_file($uuid,"$GIT_DIR/$GIT_SVN/info/uuid");

	open my $fd, '>>', "$GIT_DIR/$GIT_SVN/info/exclude" or croak $!;
	print $fd '.svn',"\n";
	close $fd or croak $!;
	return $uuid;
}

sub assert_svn_wc_clean {
	my ($svn_rev, $treeish) = @_;
	croak "$svn_rev is not an integer!\n" unless ($svn_rev =~ /^\d+$/);
	croak "$treeish is not a sha1!\n" unless ($treeish =~ /^$sha1$/o);
	my $svn_info = svn_info('.');
	if ($svn_rev != $svn_info->{'Last Changed Rev'}) {
		croak "Expected r$svn_rev, got r",
				$svn_info->{'Last Changed Rev'},"\n";
	}
	my @status = grep(!/^Performing status on external/,(`svn status`));
	@status = grep(!/^\s*$/,@status);
	if (scalar @status) {
		print STDERR "Tree ($SVN_WC) is not clean:\n";
		print STDERR $_ foreach @status;
		croak;
	}
	assert_tree($treeish);
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
	git_addremove();
	chomp(my $tree = `git-write-tree`);
	if ($old_index) {
		$ENV{GIT_INDEX_FILE} = $old_index;
	} else {
		delete $ENV{GIT_INDEX_FILE};
	}
	if ($tree ne $expected) {
		croak "Tree mismatch, Got: $tree, Expected: $expected\n";
	}
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
	assert_svn_wc_clean($svn_rev,$from);
	print "diff-tree '$from' '$treeish'\n";
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
		# git can do empty commits, SVN doesn't allow it...
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
	open my $msg, '>', $commit_msg  or croak $!;

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
	my @ci_output = safe_qx(qw(svn commit -F),$commit_msg);
	my ($committed) = grep(/^Committed revision \d+\./,@ci_output);
	unlink $commit_msg;
	defined $committed or croak
			"Commit output failed to parse committed revision!\n",
			join("\n",@ci_output),"\n";
	my ($rev_committed) = ($committed =~ /^Committed revision (\d+)\./);

	# resync immediately
	my @svn_up = (qw(svn up), "-r$svn_rev");
	push @svn_up, '--ignore-externals' unless $_no_ignore_ext;
	sys(@svn_up);
	return fetch("$rev_committed=$commit")->{revision};
}

sub svn_log_raw {
	my (@log_args) = @_;
	my $pid = open my $log_fh,'-|';
	defined $pid or croak $!;

	if ($pid == 0) {
		exec (qw(svn log), @log_args) or croak $!
	}

	my @svn_log;
	my $state = 'sep';
	while (<$log_fh>) {
		chomp;
		if (/^\-{72}$/) {
			if ($state eq 'msg') {
				if ($svn_log[$#svn_log]->{lines}) {
					$svn_log[$#svn_log]->{msg} .= $_."\n";
					unless(--$svn_log[$#svn_log]->{lines}) {
						$state = 'sep';
					}
				} else {
					croak "Log parse error at: $_\n",
						$svn_log[$#svn_log]->{revision},
						"\n";
				}
				next;
			}
			if ($state ne 'sep') {
				croak "Log parse error at: $_\n",
					"state: $state\n",
					$svn_log[$#svn_log]->{revision},
					"\n";
			}
			$state = 'rev';

			# if we have an empty log message, put something there:
			if (@svn_log) {
				$svn_log[$#svn_log]->{msg} ||= "\n";
				delete $svn_log[$#svn_log]->{lines};
			}
			next;
		}
		if ($state eq 'rev' && s/^r(\d+)\s*\|\s*//) {
			my $rev = $1;
			my ($author, $date, $lines) = split(/\s*\|\s*/, $_, 3);
			($lines) = ($lines =~ /(\d+)/);
			my ($Y,$m,$d,$H,$M,$S,$tz) = ($date =~
					/(\d{4})\-(\d\d)\-(\d\d)\s
					 (\d\d)\:(\d\d)\:(\d\d)\s([\-\+]\d+)/x)
					 or croak "Failed to parse date: $date\n";
			my %log_msg = (	revision => $rev,
					date => "$tz $Y-$m-$d $H:$M:$S",
					author => $author,
					lines => $lines,
					msg => '' );
			push @svn_log, \%log_msg;
			$state = 'msg_start';
			next;
		}
		# skip the first blank line of the message:
		if ($state eq 'msg_start' && /^$/) {
			$state = 'msg';
		} elsif ($state eq 'msg') {
			if ($svn_log[$#svn_log]->{lines}) {
				$svn_log[$#svn_log]->{msg} .= $_."\n";
				unless (--$svn_log[$#svn_log]->{lines}) {
					$state = 'sep';
				}
			} else {
				croak "Log parse error at: $_\n",
					$svn_log[$#svn_log]->{revision},"\n";
			}
		}
	}
	close $log_fh or croak $?;
	return \@svn_log;
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
		if (m#^([^:]+)\s*:\s*(\S*)$#) {
			$ret->{$1} = $2;
			push @{$ret->{-order}}, $1;
		}
	}
	close $info_fh or croak $!;
	return $ret;
}

sub sys { system(@_) == 0 or croak $? }

sub git_addremove {
	system( "git-diff-files --name-only -z ".
				" | git-update-index --remove -z --stdin && ".
		"git-ls-files -z --others ".
			"'--exclude-from=$GIT_DIR/$GIT_SVN/info/exclude'".
				" | git-update-index --add -z --stdin"
		) == 0 or croak $?
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

sub assert_revision_eq_or_unknown {
	my ($revno, $commit) = @_;
	if (-f "$REV_DIR/$revno") {
		my $current = file_to_s("$REV_DIR/$revno");
		if ($commit ne $current) {
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
	my $info = svn_info('.');
	my $uuid = $info->{'Repository UUID'};
	defined $uuid or croak "Unable to get Repository UUID\n";

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
		git_addremove();
		chomp(my $tree = `git-write-tree`);
		croak if $?;
		my $msg_fh = IO::File->new_tmpfile or croak $!;
		print $msg_fh $log_msg->{msg}, "\ngit-svn-id: ",
					"$SVN_URL\@$log_msg->{revision}",
					" $uuid\n" or croak $!;
		$msg_fh->flush == 0 or croak $!;
		seek $msg_fh, 0, 0 or croak $!;

		$ENV{GIT_AUTHOR_NAME} = $ENV{GIT_COMMITTER_NAME} =
						$log_msg->{author};
		$ENV{GIT_AUTHOR_EMAIL} = $ENV{GIT_COMMITTER_EMAIL} =
						$log_msg->{author}."\@$uuid";
		$ENV{GIT_AUTHOR_DATE} = $ENV{GIT_COMMITTER_DATE} =
						$log_msg->{date};
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
	my @update_ref = ('git-update-ref',"refs/heads/$GIT_SVN-HEAD",$commit);
	if (my $primary_parent = shift @exec_parents) {
		push @update_ref, $primary_parent;
	}
	sys(@update_ref);
	sys('git-update-ref',"$GIT_SVN/revs/$log_msg->{revision}",$commit);
	print "r$log_msg->{revision} = $commit\n";
	return $commit;
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

sub svn_check_ignore_externals {
	return if $_no_ignore_ext;
	unless (grep /ignore-externals/,(safe_qx(qw(svn co -h)))) {
		print STDERR "W: Installed svn version does not support ",
				"--ignore-externals\n";
		$_no_ignore_ext = 1;
	}
}
__END__

Data structures:

@svn_log = array of log_msg hashes

$log_msg hash
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
	chg => change type [MCRAD],
	file_a => original file name of a file (iff chg is 'C' or 'R')
	file_b => new/current file name of a file (any chg)
}
;
