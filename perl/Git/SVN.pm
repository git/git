package Git::SVN;
use strict;
use warnings $ENV{GIT_PERL_FATAL_WARNINGS} ? qw(FATAL all) : ();
use Fcntl qw/:DEFAULT :seek/;
use constant rev_map_fmt => 'NH*';
use vars qw/$_no_metadata
            $_repack $_repack_flags $_use_svm_props $_head
            $_use_svnsync_props $no_reuse_existing
	    $_use_log_author $_add_author_from $_localtime/;
use Carp qw/croak/;
use File::Path qw/mkpath/;
use IPC::Open3;
use Memoize;  # core since 5.8.0, Jul 2002
use POSIX qw(:signal_h);
use Time::Local;

use Git qw(
    command
    command_oneline
    command_noisy
    command_output_pipe
    command_close_pipe
    get_tz_offset
);
use Git::SVN::Utils qw(
	fatal
	can_compress
	join_paths
	canonicalize_path
	canonicalize_url
	add_path_to_url
);

my $memo_backend;
our $_follow_parent  = 1;
our $_minimize_url   = 'unset';
our $default_repo_id = 'svn';
our $default_ref_id  = $ENV{GIT_SVN_ID} || 'git-svn';

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
			if (!defined($u)) {
				warn
"W: $refname: no associated commit metadata from SVN, skipping\n";
				next;
			}
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
			$r->{$1}->{url} = canonicalize_url($2);
		} elsif (m!^(.+)\.pushurl=\s*(.*)\s*$!) {
			$r->{$1}->{pushurl} = canonicalize_url($2);
		} elsif (m!^(.+)\.ignore-refs=\s*(.*)\s*$!) {
			$r->{$1}->{ignore_refs_regex} = $2;
		} elsif (m!^(.+)\.(branches|tags)=$svn_refspec$!) {
			my ($remote, $t, $local_ref, $remote_ref) =
			                                     ($1, $2, $3, $4);
			die("svn-remote.$remote: remote ref '$remote_ref' ($t) "
			    . "must start with 'refs/'\n")
				unless $remote_ref =~ m{^refs/};
			$local_ref = uri_decode($local_ref);

			require Git::SVN::GlobSpec;
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
	$url = canonicalize_url($url);
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
			my $old_path = $self->path;
			$url =~ s!^\Q$min_url\E(/|$)!!;
			$url = join_paths($url, $old_path);
			$self->path($url);
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
		my $path = $self->path;
		$path =~ s{^/}{};
		$path =~ s{%([0-9A-F]{2})}{chr hex($1)}ieg;
		$self->path($path);
		command_noisy('config', '--add',
			      "svn-remote.$self->{repo_id}.fetch",
			      $self->path.":".$self->refname);
	}
	$self->url($url);
}

sub find_by_url { # repos_root and, path are optional
	my ($class, $full_url, $repos_root, $path) = @_;

	$full_url = canonicalize_url($full_url);

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

		# remote fetch paths are not URI escaped.  Decode ours
		# so they match
		$p = uri_decode($p);

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
	if (!defined $self->path || !length $self->path) {
		my $fetch = command_oneline('config', '--get',
		                            "svn-remote.$repo_id.fetch",
		                            ":$ref_id\$") or
		     die "Failed to read \"svn-remote.$repo_id.fetch\" ",
		         "\":$ref_id\$\" in config\n";
		my($path) = split(/\s*:\s*/, $fetch);
		$self->path($path);
	}
	{
		my $path = $self->path;
		$path =~ s{\A/}{};
		$path =~ s{/\z}{};
		$self->path($path);
	}
	my $url = command_oneline('config', '--get',
	                          "svn-remote.$repo_id.url") or
                  die "Failed to read \"svn-remote.$repo_id.url\" in config\n";
	$self->url($url);
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
		die "ref: '$refname' ends with a trailing slash; this is ",
		    "not permitted by git or Subversion\n";
	}

	# It cannot have ASCII control character space, tilde ~, caret ^,
	# colon :, question-mark ?, asterisk *, space, or open bracket [
	# anywhere.
	#
	# Additionally, % must be escaped because it is used for escaping
	# and we want our escaped refname to be reversible
	$refname =~ s{([ \%~\^:\?\*\[\t\\])}{sprintf('%%%02X',ord($1))}eg;

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

		my $replace = add_path_to_url($ra->url, $path);

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
	my $path = $self->path;
	my %tried;
	while (length $path) {
		my $try = add_path_to_url($self->url, $path);
		unless ($tried{$try}) {
			return $ra if $self->read_svm_props($ra, $path, $r);
			$tried{$try} = 1;
		}
		$path =~ s#/?[^/]+$##;
	}
	die "Path: '$path' should be ''\n" if $path ne '';
	return $ra if $self->read_svm_props($ra, $path, $r);
	$tried{ add_path_to_url($self->url, $path) } = 1;

	if ($ra->{repos_root} eq $self->url) {
		die @err, (map { "  $_\n" } keys %tried), "\n";
	}

	# nope, make sure we're connected to the repository root:
	my $ok;
	my @tried_b;
	$path = $ra->{svn_path};
	$ra = Git::SVN::Ra->new($ra->{repos_root});
	while (length $path) {
		my $try = add_path_to_url($ra->url, $path);
		unless ($tried{$try}) {
			$ok = $self->read_svm_props($ra, $path, $r);
			last if $ok;
			$tried{$try} = 1;
		}
		$path =~ s#/?[^/]+$##;
	}
	die "Path: '$path' should be ''\n" if $path ne '';
	$ok ||= $self->read_svm_props($ra, $path, $r);
	$tried{ add_path_to_url($ra->url, $path) } = 1;
	if (!$ok) {
		die @err, (map { "  $_\n" } keys %tried), "\n";
	}
	Git::SVN::Ra->new($self->url);
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
			die "ra_uuid called without URL\n" unless $self->url;
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
	my $ra = Git::SVN::Ra->new($self->url);
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
	$p =~ s#^/+\Q@{[$self->path]}\E(/|$)#/#;
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
		$self->prop_walk($self->path . $p . $_, $rev, $sub);
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

sub svn_dir {
	command_oneline(qw(rev-parse --git-path svn));
}

sub tmp_config {
	my (@args) = @_;
	my $svn_dir = svn_dir();
	my $old_def_config = "$svn_dir/config";
	my $config = "$svn_dir/.metadata";
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
		           /^tree ($::oid)/mo);
		return if $y eq $x;

		warn "Index mismatch: $y != $x\nrereading $treeish\n";
		unlink $self->{index} or die "unlink $self->{index}: $!\n";
		command_noisy('read-tree', $treeish);
		$x = command_oneline('write-tree');
		if ($y ne $x) {
			fatal "trees ($treeish) $y != $x\n",
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
	my $url = $self->rewrite_root || $self->url;
	return canonicalize_url( add_path_to_url( $url, $self->path ) );
}

sub full_url {
	my ($self) = @_;
	return canonicalize_url( add_path_to_url( $self->url, $self->path ) );
}

sub full_pushurl {
	my ($self) = @_;
	if ($self->{pushurl}) {
		return canonicalize_url( add_path_to_url( $self->{pushurl}, $self->path ) );
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
	die "Tree is not a valid oid $tree\n" if $tree !~ /^$::oid$/o;

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
	if ($commit !~ /^$::oid$/o) {
		die "Failed to commit, invalid oid: $commit\n";
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
	return 1 if $self->path eq '';
	if (my $path = $paths->{"/".$self->path}) {
		return ($path->{action} eq 'D') ? 0 : 1;
	}
	$self->{path_regex} ||= qr{^/\Q@{[$self->path]}\E/};
	if (grep /$self->{path_regex}/, keys %$paths) {
		return 1;
	}
	my $c = '';
	foreach (split m#/#, $self->path) {
		$c .= "/$_";
		next unless ($paths->{$c} &&
		             ($paths->{$c}->{action} =~ /^[AR]$/));
		if ($self->ra->check_path($self->path, $r) ==
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
		$self->ra->get_log([$self->path], $rev, $rev, 0, 1, 1,
				   sub { $paths = $_[0] });
		$SVN::Error::handler = $err_handler;
	}
	return undef unless defined $paths;

	# look for a parent from another branch:
	my @b_path_components = split m#/#, $self->path;
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
	my $url = $self->ra->url;
	my $new_url = canonicalize_url( add_path_to_url( $url, $branch_from ) );
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
				$gs->ra->get_log([$gs->path], $r0 + 1, $r, 1,
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
			$ed = Git::SVN::Fetcher->new($self, $gs->path);
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
			$ed = Git::SVN::Fetcher->new($self);
			$self->ra->gs_do_update($rev, $rev, $self, $ed)
			  or die "SVN connection failed somewhere...\n";
		}
		print STDERR "Successfully followed parent\n" unless $::_q > 1;
		return $self->make_log_entry($rev, [$parent], $ed, $r0, $branch_from);
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
		# want to break the original ref or lose copypath info):
		if (my $log_entry = $self->find_parent_branch($paths, $rev)) {
			push @{$log_entry->{parents}}, $lc;
			return $log_entry;
		}
		$ed = Git::SVN::Fetcher->new($self);
		$last_rev = $self->{last_rev};
		$ed->{c} = $lc;
		@parents = ($lc);
	} else {
		$last_rev = $rev;
		if (my $log_entry = $self->find_parent_branch($paths, $rev)) {
			return $log_entry;
		}
		$ed = Git::SVN::Fetcher->new($self);
	}
	unless ($self->ra->gs_do_update($last_rev, $rev, $self, $ed)) {
		die "SVN connection failed somewhere...\n";
	}
	$self->make_log_entry($rev, \@parents, $ed, $last_rev, $self->path);
}

sub mkemptydirs {
	my ($self, $r) = @_;

	# add/remove/collect a paths table
	#
	# Paths are split into a tree of nodes, stored as a hash of hashes.
	#
	# Each node contains a 'path' entry for the path (if any) associated
	# with that node and a 'children' entry for any nodes under that
	# location.
	#
	# Removing a path requires a hash lookup for each component then
	# dropping that node (and anything under it), which is substantially
	# faster than a grep slice into a single hash of paths for large
	# numbers of paths.
	#
	# For a large (200K) number of empty_dir directives this reduces
	# scanning time to 3 seconds vs 10 minutes for grep+delete on a single
	# hash of paths.
	sub add_path {
		my ($paths_table, $path) = @_;
		my $node_ref;

		foreach my $x (split('/', $path)) {
			if (!exists($paths_table->{$x})) {
				$paths_table->{$x} = { children => {} };
			}

			$node_ref = $paths_table->{$x};
			$paths_table = $paths_table->{$x}->{children};
		}

		$node_ref->{path} = $path;
	}

	sub remove_path {
		my ($paths_table, $path) = @_;
		my $nodes_ref;
		my $node_name;

		foreach my $x (split('/', $path)) {
			if (!exists($paths_table->{$x})) {
				return;
			}

			$nodes_ref = $paths_table;
			$node_name = $x;

			$paths_table = $paths_table->{$x}->{children};
		}

		delete($nodes_ref->{$node_name});
	}

	sub collect_paths {
		my ($paths_table, $paths_ref) = @_;

		foreach my $v (values %$paths_table) {
			my $p = $v->{path};
			my $c = $v->{children};

			collect_paths($c, $paths_ref);

			if (defined($p)) {
				push(@$paths_ref, $p);
			}
		}
	}

	sub scan {
		my ($r, $paths_table, $line) = @_;
		if (defined $r && $line =~ /^r(\d+)$/) {
			return 0 if $1 > $r;
		} elsif ($line =~ /^  \+empty_dir: (.+)$/) {
			add_path($paths_table, $1);
		} elsif ($line =~ /^  \-empty_dir: (.+)$/) {
			remove_path($paths_table, $1);
		}
		1; # continue
	};

	my @empty_dirs;
	my %paths_table;

	my $gz_file = "$self->{dir}/unhandled.log.gz";
	if (-f $gz_file) {
		if (!can_compress()) {
			warn "Compress::Zlib could not be found; ",
			     "empty directories in $gz_file will not be read\n";
		} else {
			my $gz = Compress::Zlib::gzopen($gz_file, "rb") or
				die "Unable to open $gz_file: $!\n";
			my $line;
			while ($gz->gzreadline($line) > 0) {
				scan($r, \%paths_table, $line) or last;
			}
			$gz->gzclose;
		}
	}

	if (open my $fh, '<', "$self->{dir}/unhandled.log") {
		binmode $fh or croak "binmode: $!";
		while (<$fh>) {
			scan($r, \%paths_table, $_) or last;
		}
		close $fh;
	}

	collect_paths(\%paths_table, \@empty_dirs);
	my $strip = qr/\A\Q@{[$self->path]}\E(?:\/|$)/;
	foreach my $d (sort @empty_dirs) {
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
	                                    (\d\d?)\:(\d\d)\:(\d\d)\.\d*Z$/x) or
	                                 croak "Unable to parse date: $date\n";
	my $parsed_date;    # Set next.

	if ($Git::SVN::_localtime) {
		# Translate the Subversion datetime to an epoch time.
		# Begin by switching ourselves to $date's timezone, UTC.
		my $old_env_TZ = $ENV{TZ};
		$ENV{TZ} = 'UTC';

		my $epoch_in_UTC =
		    Time::Local::timelocal($S, $M, $H, $d, $m - 1, $Y);

		# Determine our local timezone (including DST) at the
		# time of $epoch_in_UTC.  $Git::SVN::Log::TZ stored the
		# value of TZ, if any, at the time we were run.
		if (defined $Git::SVN::Log::TZ) {
			$ENV{TZ} = $Git::SVN::Log::TZ;
		} else {
			delete $ENV{TZ};
		}

		my $our_TZ = get_tz_offset($epoch_in_UTC);

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
		return [$name, $email];
	} else {
		die "Author: $orig_author: $::_authors_prog returned "
			. "invalid author format: $author\n";
	}
}

sub check_author {
	my ($author) = @_;
	if (defined $author) {
		$author =~ s/^\s+//g;
		$author =~ s/\s+$//g;
	}
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
	my ($self, $tickets, $parents) = @_;
	# aha!  svk:merge property changed...
	my @tickets = split "\n", $tickets;
	my @known_parents;
	for my $ticket ( @tickets ) {
		my ($uuid, $path, $rev) = split /:/, $ticket;
		if ( $uuid eq $self->ra_uuid ) {
			my $repos_root = $self->url;
			my $branch_from = $path;
			$branch_from =~ s{^/}{};
			my $gs = $self->other_gs(add_path_to_url( $repos_root, $branch_from ),
			                         $repos_root,
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
	my $source = shift;
	my $revs = shift;

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
		if ($range =~ /[*]$/) {
			warn "W: Ignoring partial merge in svn:mergeinfo "
				."dirprop: $source:$range\n";
			next;
		}
		my ($bottom, $top) = split "-", $range;
		$top ||= $bottom;
		my $bottom_commit = $gs->find_rev_after( $bottom, 1, $top );
		my $top_commit = $gs->find_rev_before( $top, 1, $bottom );

		unless ($top_commit and $bottom_commit) {
			warn "W: unknown path/rev in svn:mergeinfo "
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

sub check_cherry_pick2 {
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
	my @k = (keys %commits);
	return (scalar @k, $k[0]);
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

sub tie_for_persistent_memoization {
	my $hash = shift;
	my $path = shift;

	unless ($memo_backend) {
		if (eval { require Git::SVN::Memoize::YAML; 1}) {
			$memo_backend = 1;
		} else {
			require Memoize::Storable;
			$memo_backend = -1;
		}
	}

	if ($memo_backend > 0) {
		tie %$hash => 'Git::SVN::Memoize::YAML', "$path.yaml";
	} else {
		# first verify that any existing file can actually be loaded
		# (it may have been saved by an incompatible version)
		my $db = "$path.db";
		if (-e $db) {
			use Storable qw(retrieve);

			if (!eval { retrieve($db); 1 }) {
				unlink $db or die "unlink $db failed: $!";
			}
		}
		tie %$hash => 'Memoize::Storable', $db, 'nstore';
	}
}

# The GIT_DIR environment variable is not always set until after the command
# line arguments are processed, so we can't memoize in a BEGIN block.
{
	my $memoized = 0;

	sub memoize_svn_mergeinfo_functions {
		return if $memoized;
		$memoized = 1;

		my $cache_path = svn_dir() . '/.caches/';
		mkpath([$cache_path]) unless -d $cache_path;

		my %lookup_svn_merge_cache;
		my %check_cherry_pick2_cache;
		my %has_no_changes_cache;

		tie_for_persistent_memoization(\%lookup_svn_merge_cache,
		    "$cache_path/lookup_svn_merge");
		memoize 'lookup_svn_merge',
			SCALAR_CACHE => 'FAULT',
			LIST_CACHE => ['HASH' => \%lookup_svn_merge_cache],
		;

		tie_for_persistent_memoization(\%check_cherry_pick2_cache,
		    "$cache_path/check_cherry_pick2");
		memoize 'check_cherry_pick2',
			SCALAR_CACHE => 'FAULT',
			LIST_CACHE => ['HASH' => \%check_cherry_pick2_cache],
		;

		tie_for_persistent_memoization(\%has_no_changes_cache,
		    "$cache_path/has_no_changes");
		memoize 'has_no_changes',
			SCALAR_CACHE => ['HASH' => \%has_no_changes_cache],
			LIST_CACHE => 'FAULT',
		;
	}

	sub unmemoize_svn_mergeinfo_functions {
		return if not $memoized;
		$memoized = 0;

		Memoize::unmemoize 'lookup_svn_merge';
		Memoize::unmemoize 'check_cherry_pick2';
		Memoize::unmemoize 'has_no_changes';
	}

	sub clear_memoized_mergeinfo_caches {
		die "Only call this method in non-memoized context" if ($memoized);

		my $cache_path = svn_dir() . '/.caches/';
		return unless -d $cache_path;

		for my $cache_file (("$cache_path/lookup_svn_merge",
				     "$cache_path/check_cherry_pick", # old
				     "$cache_path/check_cherry_pick2",
				     "$cache_path/has_no_changes")) {
			for my $suffix (qw(yaml db)) {
				my $file = "$cache_file.$suffix";
				next unless -e $file;
				unlink($file) or die "unlink($file) failed: $!\n";
			}
		}
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

# Compute what's new in svn:mergeinfo.
sub mergeinfo_changes {
	my ($self, $old_path, $old_rev, $path, $rev, $mergeinfo_prop) = @_;
	my %minfo = map {split ":", $_ } split "\n", $mergeinfo_prop;
	my $old_minfo = {};

	my $ra = $self->ra;
	# Give up if $old_path isn't in the repo.
	# This is probably a merge on a subtree.
	if ($ra->check_path($old_path, $old_rev) != $SVN::Node::dir) {
		warn "W: ignoring svn:mergeinfo on $old_path, ",
			"directory didn't exist in r$old_rev\n";
		return {};
	}
	my (undef, undef, $props) = $ra->get_dir($old_path, $old_rev);
	if (defined $props->{"svn:mergeinfo"}) {
		my %omi = map {split ":", $_ } split "\n",
			$props->{"svn:mergeinfo"};
		$old_minfo = \%omi;
	}

	my %changes = ();
	foreach my $p (keys %minfo) {
		my $a = $old_minfo->{$p} || "";
		my $b = $minfo{$p};
		# Omit merged branches whose ranges lists are unchanged.
		next if $a eq $b;
		# Remove any common range list prefix.
		($a ^ $b) =~ /^[\0]*/;
		my $common_prefix = rindex $b, ",", $+[0] - 1;
		$changes{$p} = substr $b, $common_prefix + 1;
	}
	print STDERR "Checking svn:mergeinfo changes since r$old_rev: ",
		scalar(keys %minfo), " sources, ",
		scalar(keys %changes), " changed\n";

	return \%changes;
}

# note: this function should only be called if the various dirprops
# have actually changed
sub find_extra_svn_parents {
	my ($self, $mergeinfo, $parents) = @_;
	# aha!  svk:merge property changed...

	memoize_svn_mergeinfo_functions();

	# We first search for merged tips which are not in our
	# history.  Then, we figure out which git revisions are in
	# that tip, but not this revision.  If all of those revisions
	# are now marked as merge, we can add the tip as a parent.
	my @merges = sort keys %$mergeinfo;
	my @merge_tips;
	my $url = $self->url;
	my $uuid = $self->ra_uuid;
	my @all_ranges;
	for my $merge ( @merges ) {
		my ($tip_commit, @ranges) =
			lookup_svn_merge( $uuid, $url,
					  $merge, $mergeinfo->{$merge} );
		unless (!$tip_commit or
				grep { $_ eq $tip_commit } @$parents ) {
			push @merge_tips, $tip_commit;
			push @all_ranges, @ranges;
		} else {
			push @merge_tips, undef;
		}
	}

	my %excluded = map { $_ => 1 }
		parents_exclude($parents, grep { defined } @merge_tips);

	# check merge tips for new parents
	my @new_parents;
	for my $merge_tip ( @merge_tips ) {
		my $merge = shift @merges;
		next unless $merge_tip and $excluded{$merge_tip};
		my $spec = "$merge:$mergeinfo->{$merge}";

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
		my ($ninc, $ifirst) = check_cherry_pick2(
			$merge_base, $merge_tip,
			$parents,
			@all_ranges,
		       );

		if ($ninc) {
			warn "W: svn cherry-pick ignored ($spec) - missing " .
				"$ninc commit(s) (eg $ifirst)\n";
		} else {
			warn "Found merge parent ($spec): ", $merge_tip, "\n";
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
	my ($self, $rev, $parents, $ed, $parent_rev, $parent_path) = @_;
	my $untracked = $self->get_untracked($ed);

	my @parents = @$parents;
	my $props = $ed->{dir_prop}{$self->path};
	if ($self->follow_parent) {
		my $tickets = $props->{"svk:merge"};
		if ($tickets) {
			$self->find_extra_svk_parents($tickets, \@parents);
		}

		my $mergeinfo_prop = $props->{"svn:mergeinfo"};
		if ($mergeinfo_prop) {
			my $mi_changes = $self->mergeinfo_changes(
						$parent_path,
						$parent_rev,
						$self->path,
						$rev,
						$mergeinfo_prop);
			$self->find_extra_svn_parents($mi_changes, \@parents);
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
		$email = "$author\@$uuid" unless defined $email;
		$commit_email = "$author\@$uuid" unless defined $commit_email;
	} elsif ($self->use_svnsync_props) {
		my $full_url = canonicalize_url(
			add_path_to_url( $self->svnsync->{url}, $self->path )
		);
		remove_username($full_url);
		my $uuid = $self->svnsync->{uuid};
		$log_entry{metadata} = "$full_url\@$rev $uuid";
		$email = "$author\@$uuid" unless defined $email;
		$commit_email = "$author\@$uuid" unless defined $commit_email;
	} else {
		my $url = $self->metadata_url;
		remove_username($url);
		my $uuid = $self->rewrite_uuid || $self->ra->get_uuid;
		$log_entry{metadata} = "$url\@$rev " . $uuid;
		$email = "$author\@$uuid" unless defined $email;
		$commit_email = "$author\@$uuid" unless defined $commit_email;
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
		fatal("Must have an existing revision to commit");
	}
	my %ed_opts = ( r => $self->{last_rev},
	                log => $log_entry->{log},
	                ra => $self->ra,
	                tree_a => $self->{last_commit},
	                tree_b => $tree,
	                editor_cb => sub {
			       $self->set_tree_cb($log_entry, $tree, @_) },
	                svn_path => $self->path );
	if (!Git::SVN::Editor->new(\%ed_opts)->apply_diff) {
		print "No changes\nr$self->{last_rev} = $tree\n";
	}
}

sub rebuild_from_rev_db {
	my ($self, $path) = @_;
	my $r = -1;
	open my $fh, '<', $path or croak "open: $!";
	binmode $fh or croak "binmode: $!";
	while (<$fh>) {
		length($_) == $::oid_length + 1 or croak "inconsistent size in ($_)";
		chomp($_);
		++$r;
		next if $_ eq ('0' x $::oid_length);
		$self->rev_map_set($r, $_);
		print "r$r = $_\n";
	}
	close $fh or croak "close: $!";
	unlink $path or croak "unlink: $!";
}

#define a global associate map to record rebuild status
my %rebuild_status;
#define a global associate map to record rebuild verify status
my %rebuild_verify_status;

sub rebuild {
	my ($self) = @_;
	my $map_path = $self->map_path;
	my $partial = (-e $map_path && ! -z $map_path);
	my $verify_key = $self->refname.'^0';
	if (!$rebuild_verify_status{$verify_key}) {
		my $verify_result = ::verify_ref($verify_key);
		if ($verify_result) {
			$rebuild_verify_status{$verify_key} = 1;
		}
	}
	if (!$rebuild_verify_status{$verify_key}) {
		return;
	}
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
	my $key_value = ($head ? "$head.." : "") . $self->refname;
	if (exists $rebuild_status{$key_value}) {
		print "Done rebuilding $map_path\n" if (!$partial || !$head);
		my $rev_db_path = $self->rev_db_path;
		if (-f $self->rev_db_path) {
			unlink $self->rev_db_path or croak "unlink: $!";
		}
		$self->unlink_rev_db_symlink;
		return;
	}
	my ($log, $ctx) =
		command_output_pipe(qw/rev-list --pretty=raw --reverse/,
				$key_value,
				'--');
	$rebuild_status{$key_value} = 1;
	my $metadata_url = $self->metadata_url;
	remove_username($metadata_url);
	my $svn_uuid = $self->rewrite_uuid || $self->ra_uuid;
	my $c;
	while (<$log>) {
		if ( m{^commit ($::oid)$} ) {
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
#   - 24 or 36 bytes for every record,
#     * 4 bytes for the integer representing an SVN revision number
#     * 20 or 32 bytes representing the oid of a git commit
#   - No empty padding records like the old format
#     (except the last record, which can be overwritten)
#   - new records are written append-only since SVN revision numbers
#     increase monotonically
#   - lookups on SVN revision number are done via a binary search
#   - Piping the file to xxd -c24 is a good way of dumping it for
#     viewing or editing (piped back through xxd -r), should the need
#     ever arise.
#   - The last record can be padding revision with an all-zero oid
#     This is used to optimize fetch performance when using multiple
#     "fetch" directives in .git/config
#
# These files are disposable unless noMetadata or useSvmProps is set

sub _rev_map_set {
	my ($fh, $rev, $commit) = @_;
	my $record_size = ($::oid_length / 2) + 4;

	binmode $fh or croak "binmode: $!";
	my $size = (stat($fh))[7];
	($size % $record_size) == 0 or croak "inconsistent size: $size";

	my $wr_offset = 0;
	if ($size > 0) {
		sysseek($fh, -$record_size, SEEK_END) or croak "seek: $!";
		my $read = sysread($fh, my $buf, $record_size) or croak "read: $!";
		$read == $record_size or croak "read only $read bytes (!= $record_size)";
		my ($last_rev, $last_commit) = unpack(rev_map_fmt, $buf);
		if ($last_commit eq ('0' x $::oid_length)) {
			if ($size >= ($record_size * 2)) {
				sysseek($fh, -($record_size * 2), SEEK_END) or croak "seek: $!";
				$read = sysread($fh, $buf, $record_size) or
				    croak "read: $!";
				$read == $record_size or
				    croak "read only $read bytes (!= $record_size)";
				($last_rev, $last_commit) =
				    unpack(rev_map_fmt, $buf);
				if ($last_commit eq ('0' x $::oid_length)) {
					croak "inconsistent .rev_map\n";
				}
			}
			if ($last_rev >= $rev) {
				croak "last_rev is higher!: $last_rev >= $rev";
			}
			$wr_offset = -$record_size;
		}
	}
	sysseek($fh, $wr_offset, SEEK_END) or croak "seek: $!";
	syswrite($fh, pack(rev_map_fmt, $rev, $commit), $record_size) == $record_size or
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
	$commit =~ /^$::oid$/ or die "arg3 must be a full hex object ID\n";
	my $db = $self->map_path($uuid);
	my $db_lock = "$db.lock";
	my $sigmask;
	$update_ref ||= 0;
	if ($update_ref) {
		$sigmask = POSIX::SigSet->new();
		my $signew = POSIX::SigSet->new(SIGINT, SIGHUP, SIGTERM,
			SIGALRM, SIGUSR1, SIGUSR2);
		sigprocmask(SIG_BLOCK, $signew, $sigmask) or
			croak "Can't block signals: $!";
	}
	mkfile($db);

	$LOCKFILES{$db_lock} = 1;
	my $sync;
	# both of these options make our .rev_db file very, very important
	# and we can't afford to lose it because rebuild() won't work
	if ($self->use_svm_props || $self->no_metadata) {
		require File::Copy;
		$sync = 1;
		File::Copy::copy($db, $db_lock) or die "rev_map_set(@_): ",
					   "Failed to copy: ",
					   "$db => $db_lock ($!)\n";
	} else {
		rename $db, $db_lock or die "rev_map_set(@_): ",
					    "Failed to rename: ",
					    "$db => $db_lock ($!)\n";
	}

	sysopen(my $fh, $db_lock, O_RDWR | O_CREAT)
	     or croak "Couldn't open $db_lock: $!\n";
	if ($update_ref eq 'reset') {
		clear_memoized_mergeinfo_caches();
		_rev_map_reset($fh, $rev, $commit);
	} else {
		_rev_map_set($fh, $rev, $commit);
	}

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
		sigprocmask(SIG_SETMASK, $sigmask) or
			croak "Can't restore signal mask: $!";
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
	my $record_size = ($::oid_length / 2) + 4;
	my $map_path = $self->map_path;
	stat $map_path or return $want_commit ? (0, undef) : 0;
	sysopen(my $fh, $map_path, O_RDONLY) or croak "open: $!";
	binmode $fh or croak "binmode: $!";
	my $size = (stat($fh))[7];
	($size % $record_size) == 0 or croak "inconsistent size: $size";

	if ($size == 0) {
		close $fh or croak "close: $!";
		return $want_commit ? (0, undef) : 0;
	}

	sysseek($fh, -$record_size, SEEK_END) or croak "seek: $!";
	sysread($fh, my $buf, $record_size) == $record_size or croak "read: $!";
	my ($r, $c) = unpack(rev_map_fmt, $buf);
	if ($want_commit && $c eq ('0' x $::oid_length)) {
		if ($size < $record_size * 2) {
			return $want_commit ? (0, undef) : 0;
		}
		sysseek($fh, -($record_size * 2), SEEK_END) or croak "seek: $!";
		sysread($fh, $buf, $record_size) == $record_size or croak "read: $!";
		($r, $c) = unpack(rev_map_fmt, $buf);
		if ($c eq ('0' x $::oid_length)) {
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
	my $record_size = ($::oid_length / 2) + 4;

	binmode $fh or croak "binmode: $!";
	my $size = (stat($fh))[7];
	($size % $record_size) == 0 or croak "inconsistent size: $size";

	if ($size == 0) {
		return undef;
	}

	my ($l, $u) = (0, $size - $record_size);
	my ($r, $c, $buf);

	while ($l <= $u) {
		my $i = int(($l/$record_size + $u/$record_size) / 2) * $record_size;
		sysseek($fh, $i, SEEK_SET) or croak "seek: $!";
		sysread($fh, my $buf, $record_size) == $record_size or croak "read: $!";
		my ($r, $c) = unpack(rev_map_fmt, $buf);

		if ($r < $rev) {
			$l = $i + $record_size;
		} elsif ($r > $rev) {
			$u = $i - $record_size;
		} else { # $r == $rev
			return $c eq ('0' x $::oid_length) ? undef : $c;
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
		$repo_id = $default_repo_id;
	}
	unless (defined $ref_id && length $ref_id) {
		# Access the prefix option from the git-svn main program if it's loaded.
		my $prefix = defined &::opt_prefix ? ::opt_prefix() : "";
		$_[2] = $ref_id =
		             "refs/remotes/$prefix$default_ref_id";
	}
	$_[1] = $repo_id;
	my $svn_dir = svn_dir();
	my $dir = "$svn_dir/$ref_id";

	# Older repos imported by us used $svn_dir/foo instead of
	# $svn_dir/refs/remotes/foo when tracking refs/remotes/foo
	if ($ref_id =~ m{^refs/remotes/(.+)}) {
		my $old_dir = "$svn_dir/$1";
		if (-d $old_dir && ! -d $dir) {
			$dir = $old_dir;
		}
	}

	$_[3] = $path = '' unless (defined $path);
	mkpath([$dir]);
	my $obj = bless {
		ref_id => $ref_id, dir => $dir, index => "$dir/index",
	        config => "$svn_dir/config",
	        map_root => "$dir/.rev_map", repo_id => $repo_id }, $class;

	# Ensure it gets canonicalized
	$obj->path($path);

	return $obj;
}

sub path {
	my $self = shift;

	if (@_) {
		my $path = shift;
		$self->{_path} = canonicalize_path($path);
		return;
	}

	return $self->{_path};
}

sub url {
	my $self = shift;

	if (@_) {
		my $url = shift;
		$self->{url} = canonicalize_url($url);
		return;
	}

	return $self->{url};
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
	$f =~ s#([^a-zA-Z0-9\*!\:_\./\-])#sprintf("%%%02X",ord($1))#eg;
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

1;
