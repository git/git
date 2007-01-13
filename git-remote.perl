#!/usr/bin/perl -w

use Git;
my $git = Git->repository();

sub add_remote_config {
	my ($hash, $name, $what, $value) = @_;
	if ($what eq 'url') {
		if (exists $hash->{$name}{'URL'}) {
			print STDERR "Warning: more than one remote.$name.url\n";
		}
		$hash->{$name}{'URL'} = $value;
	}
	elsif ($what eq 'fetch') {
		$hash->{$name}{'FETCH'} ||= [];
		push @{$hash->{$name}{'FETCH'}}, $value;
	}
	if (!exists $hash->{$name}{'SOURCE'}) {
		$hash->{$name}{'SOURCE'} = 'config';
	}
}

sub add_remote_remotes {
	my ($hash, $file, $name) = @_;

	if (exists $hash->{$name}) {
		$hash->{$name}{'WARNING'} = 'ignored due to config';
		return;
	}

	my $fh;
	if (!open($fh, '<', $file)) {
		print STDERR "Warning: cannot open $file\n";
		return;
	}
	my $it = { 'SOURCE' => 'remotes' };
	$hash->{$name} = $it;
	while (<$fh>) {
		chomp;
		if (/^URL:\s*(.*)$/) {
			# Having more than one is Ok -- it is used for push.
			if (! exists $it->{'URL'}) {
				$it->{'URL'} = $1;
			}
		}
		elsif (/^Push:\s*(.*)$/) {
			; # later
		}
		elsif (/^Pull:\s*(.*)$/) {
			$it->{'FETCH'} ||= [];
			push @{$it->{'FETCH'}}, $1;
		}
		elsif (/^\#/) {
			; # ignore
		}
		else {
			print STDERR "Warning: funny line in $file: $_\n";
		}
	}
	close($fh);
}

sub list_remote {
	my ($git) = @_;
	my %seen = ();
	my @remotes = eval {
		$git->command(qw(repo-config --get-regexp), '^remote\.');
	};
	for (@remotes) {
		if (/^remote\.([^.]*)\.(\S*)\s+(.*)$/) {
			add_remote_config(\%seen, $1, $2, $3);
		}
	}

	my $dir = $git->repo_path() . "/remotes";
	if (opendir(my $dh, $dir)) {
		local $_;
		while ($_ = readdir($dh)) {
			chomp;
			next if (! -f "$dir/$_" || ! -r _);
			add_remote_remotes(\%seen, "$dir/$_", $_);
		}
	}

	return \%seen;
}

sub add_branch_config {
	my ($hash, $name, $what, $value) = @_;
	if ($what eq 'remote') {
		if (exists $hash->{$name}{'REMOTE'}) {
			print STDERR "Warning: more than one branch.$name.remote\n";
		}
		$hash->{$name}{'REMOTE'} = $value;
	}
	elsif ($what eq 'merge') {
		$hash->{$name}{'MERGE'} ||= [];
		push @{$hash->{$name}{'MERGE'}}, $value;
	}
}

sub list_branch {
	my ($git) = @_;
	my %seen = ();
	my @branches = eval {
		$git->command(qw(repo-config --get-regexp), '^branch\.');
	};
	for (@branches) {
		if (/^branch\.([^.]*)\.(\S*)\s+(.*)$/) {
			add_branch_config(\%seen, $1, $2, $3);
		}
	}

	return \%seen;
}

my $remote = list_remote($git);
my $branch = list_branch($git);

sub update_ls_remote {
	my ($harder, $info) = @_;

	return if (($harder == 0) ||
		   (($harder == 1) && exists $info->{'LS_REMOTE'}));

	my @ref = map {
		s|^[0-9a-f]{40}\s+refs/heads/||;
		$_;
	} $git->command(qw(ls-remote --heads), $info->{'URL'});
	$info->{'LS_REMOTE'} = \@ref;
}

sub show_wildcard_mapping {
	my ($forced, $ours, $ls) = @_;
	my %refs;
	for (@$ls) {
		$refs{$_} = 01; # bit #0 to say "they have"
	}
	for ($git->command('for-each-ref', "refs/remotes/$ours")) {
		chomp;
		next unless (s|^[0-9a-f]{40}\s[a-z]+\srefs/remotes/$ours/||);
		next if ($_ eq 'HEAD');
		$refs{$_} ||= 0;
		$refs{$_} |= 02; # bit #1 to say "we have"
	}
	my (@new, @stale, @tracked);
	for (sort keys %refs) {
		my $have = $refs{$_};
		if ($have == 1) {
			push @new, $_;
		}
		elsif ($have == 2) {
			push @stale, $_;
		}
		elsif ($have == 3) {
			push @tracked, $_;
		}
	}
	if (@new) {
		print "  New remote branches (next fetch will store in remotes/$ours)\n";
		print "    @new\n";
	}
	if (@stale) {
		print "  Stale tracking branches in remotes/$ours (you'd better remove them)\n";
		print "    @stale\n";
	}
	if (@tracked) {
		print "  Tracked remote branches\n";
		print "    @tracked\n";
	}
}

sub show_mapping {
	my ($name, $info) = @_;
	my $fetch = $info->{'FETCH'};
	my $ls = $info->{'LS_REMOTE'};
	my (@stale, @tracked);

	for (@$fetch) {
		next unless (/(\+)?([^:]+):(.*)/);
		my ($forced, $theirs, $ours) = ($1, $2, $3);
		if ($theirs eq 'refs/heads/*' &&
		    $ours =~ /^refs\/remotes\/(.*)\/\*$/) {
			# wildcard mapping
			show_wildcard_mapping($forced, $1, $ls);
		}
		elsif ($theirs =~ /\*/ || $ours =~ /\*/) {
			print STDERR "Warning: unrecognized mapping in remotes.$name.fetch: $_\n";
		}
		elsif ($theirs =~ s|^refs/heads/||) {
			if (!grep { $_ eq $theirs } @$ls) {
				push @stale, $theirs;
			}
			elsif ($ours ne '') {
				push @tracked, $theirs;
			}
		}
	}
	if (@stale) {
		print "  Stale tracking branches in remotes/$name (you'd better remove them)\n";
		print "    @stale\n";
	}
	if (@tracked) {
		print "  Tracked remote branches\n";
		print "    @tracked\n";
	}
}

sub show_remote {
	my ($name, $ls_remote) = @_;
	if (!exists $remote->{$name}) {
		print STDERR "No such remote $name\n";
		return;
	}
	my $info = $remote->{$name};
	update_ls_remote($ls_remote, $info);

	print "* remote $name\n";
	print "  URL: $info->{'URL'}\n";
	for my $branchname (sort keys %$branch) {
		next if ($branch->{$branchname}{'REMOTE'} ne $name);
		my @merged = map {
			s|^refs/heads/||;
			$_;
		} split(' ',"@{$branch->{$branchname}{'MERGE'}}");
		next unless (@merged);
		print "  Remote branch(es) merged with 'git pull' while on branch $branchname\n";
		print "    @merged\n";
	}
	if ($info->{'LS_REMOTE'}) {
		show_mapping($name, $info);
	}
}

sub add_remote {
	my ($name, $url) = @_;
	if (exists $remote->{$name}) {
		print STDERR "remote $name already exists.\n";
		exit(1);
	}
	$git->command('repo-config', "remote.$name.url", $url);
	$git->command('repo-config', "remote.$name.fetch",
		      "+refs/heads/*:refs/remotes/$name/*");
}

if (!@ARGV) {
	for (sort keys %$remote) {
		print "$_\n";
	}
}
elsif ($ARGV[0] eq 'show') {
	my $ls_remote = 1;
	my $i;
	for ($i = 1; $i < @ARGV; $i++) {
		if ($ARGV[$i] eq '-n') {
			$ls_remote = 0;
		}
		else {
			last;
		}
	}
	if ($i >= @ARGV) {
		print STDERR "Usage: git remote show <remote>\n";
		exit(1);
	}
	for (; $i < @ARGV; $i++) {
		show_remote($ARGV[$i], $ls_remote);
	}
}
elsif ($ARGV[0] eq 'add') {
	if (@ARGV != 3) {
		print STDERR "Usage: git remote add <name> <url>\n";
		exit(1);
	}
	add_remote($ARGV[1], $ARGV[2]);
}
else {
	print STDERR "Usage: git remote\n";
	print STDERR "       git remote add <name> <url>\n";
	print STDERR "       git remote show <name>\n";
	exit(1);
}
