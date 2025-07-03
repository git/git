package Git::SVN::Migration;
# these version numbers do NOT correspond to actual version numbers
# of git or git-svn.  They are just relative.
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
use warnings $ENV{GIT_PERL_FATAL_WARNINGS} ? qw(FATAL all) : ();
use Carp qw/croak/;
use File::Path qw/mkpath/;
use File::Basename qw/dirname basename/;

our $_minimize;
use Git qw(
	command
	command_noisy
	command_output_pipe
	command_close_pipe
	command_oneline
);
use Git::SVN;

sub migrate_from_v0 {
	my $git_dir = $ENV{GIT_DIR};
	return undef unless -d $git_dir;
	my ($fh, $ctx) = command_output_pipe(qw/rev-parse --symbolic --all/);
	my $migrated = 0;
	while (<$fh>) {
		chomp;
		my ($id, $orig_ref) = ($_, $_);
		next unless $id =~ s#^refs/heads/(.+)-HEAD$#$1#;
		my $info_url = command_oneline(qw(rev-parse --git-path),
						"$id/info/url");
		next unless -f $info_url;
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
	my $svn_dir = Git::SVN::svn_dir();

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
		my $info_url = command_oneline(qw(rev-parse --git-path),
						"$x/info/url");
		next unless -f $info_url;
		my $u = eval { ::file_to_s($info_url) };
		next unless $u;
		my $dn = dirname("$svn_dir/$x");
		mkpath([$dn]) unless -d $dn;
		if ($x eq 'svn') { # they used 'svn' as GIT_SVN_ID:
			mkpath(["$svn_dir/svn"]);
			print STDERR " - $git_dir/$x/info => ",
			                "$svn_dir/$x/info\n";
			rename "$git_dir/$x/info", "$svn_dir/$x/info" or
			       croak "$!: $x";
			# don't worry too much about these, they probably
			# don't exist with repos this old (save for index,
			# and we can easily regenerate that)
			foreach my $f (qw/unhandled.log index .rev_db/) {
				rename "$git_dir/$x/$f", "$svn_dir/$x/$f";
			}
		} else {
			print STDERR " - $git_dir/$x => $svn_dir/$x\n";
			rename "$git_dir/$x", "$svn_dir/$x" or croak "$!: $x";
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
	my $svn_dir = Git::SVN::svn_dir();
	foreach (@dir) {
		my $x = $_;
		$x =~ s!^\Q$svn_dir\E/!!o;
		read_old_urls($l_map, $x, $_);
	}
}

sub migrate_from_v2 {
	my @cfg = command(qw/config -l/);
	return if grep /^svn-remote\..+\.url=/, @cfg;
	my %l_map;
	read_old_urls(\%l_map, '', Git::SVN::svn_dir());
	my $migrated = 0;

	require Git::SVN;
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
	require Git::SVN;
	require Git::SVN::Ra;

	my $r = Git::SVN::read_all_remotes();
	my $new_urls = {};
	my $root_repos = {};
	foreach my $repo_id (keys %$r) {
		my $url = $r->{$repo_id}->{url} or next;
		my $fetch = $r->{$repo_id}->{fetch} or next;
		my $ra = Git::SVN::Ra->new($url);

		# skip existing cases where we already connect to the root
		if (($ra->url eq $ra->{repos_root}) ||
		    ($ra->{repos_root} eq $repo_id)) {
			$root_repos->{$ra->url} = $repo_id;
			next;
		}

		my $root_ra = Git::SVN::Ra->new($ra->{repos_root});
		my $root_path = $ra->url;
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
		my $file = $ENV{GIT_CONFIG} ||
			command_oneline(qw(rev-parse --git-path config));
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

1;
