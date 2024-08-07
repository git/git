package Git::SVN::Ra;
use vars qw/@ISA $config_dir $_ignore_refs_regex $_log_window_size/;
use strict;
use warnings $ENV{GIT_PERL_FATAL_WARNINGS} ? qw(FATAL all) : ();
use Memoize;
use Git::SVN::Utils qw(
	canonicalize_url
	canonicalize_path
	add_path_to_url
);

use SVN::Ra;
BEGIN {
	@ISA = qw(SVN::Ra);
}

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

# serf has a bug that leads to a coredump upon termination if the
# remote access object is left around (not fixed yet in serf 1.3.1).
# Explicitly free it to work around the issue.
END {
	$RA = undef;
	$ra_invalid = 1;
}

sub _auth_providers () {
	require SVN::Client;
	my @rv = (
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
	);

	# earlier 1.6.x versions would segfault, and <= 1.5.x didn't have
	# this function
	if (::compare_svn_version('1.6.15') >= 0) {
		my $config = SVN::Core::config_get_config($config_dir);
		my ($p, @a);
		# config_get_config returns all config files from
		# ~/.subversion, auth_get_platform_specific_client_providers
		# just wants the config "file".
		@a = ($config->{'config'}, undef);
		$p = SVN::Core::auth_get_platform_specific_client_providers(@a);
		# Insert the return value from
		# auth_get_platform_specific_providers
		unshift @rv, @$p;
	}
	\@rv;
}

sub prepare_config_once {
	SVN::_Core::svn_config_ensure($config_dir, undef);
	my ($baton, $callbacks) = SVN::Core::auth_open_helper(_auth_providers);
	my $config = SVN::Core::config_get_config($config_dir);
	my $conf_t = $config->{'config'};

	no warnings 'once';
	# The usage of $SVN::_Core::SVN_CONFIG_* variables
	# produces warnings that variables are used only once.
	# I had not found the better way to shut them up, so
	# the warnings of type 'once' are disabled in this block.
	if (SVN::_Core::svn_config_get_bool($conf_t,
	    $SVN::_Core::SVN_CONFIG_SECTION_AUTH,
	    $SVN::_Core::SVN_CONFIG_OPTION_STORE_PASSWORDS,
	    1) == 0) {
		my $val = '1';
		if (::compare_svn_version('1.9.0') < 0) { # pre-SVN r1553823
			my $dont_store_passwords = 1;
			$val = bless \$dont_store_passwords, "_p_void";
		}
		SVN::_Core::svn_auth_set_parameter($baton,
		    $SVN::_Core::SVN_AUTH_PARAM_DONT_STORE_PASSWORDS,
		    $val);
	}
	if (SVN::_Core::svn_config_get_bool($conf_t,
	    $SVN::_Core::SVN_CONFIG_SECTION_AUTH,
	    $SVN::_Core::SVN_CONFIG_OPTION_STORE_AUTH_CREDS,
	    1) == 0) {
		$Git::SVN::Prompt::_no_auth_cache = 1;
	}

	return ($config, $baton, $callbacks);
} # no warnings 'once'

INIT {
	Memoize::memoize '_auth_providers';
	Memoize::memoize 'prepare_config_once';
}

sub new {
	my ($class, $url) = @_;
	$url = canonicalize_url($url);
	return $RA if ($RA && $RA->url eq $url);

	::_req_svn();

	$RA = undef;
	my ($config, $baton, $callbacks) = prepare_config_once();
	my $self = SVN::Ra->new(url => $url, auth => $baton,
	                      config => $config,
			      pool => SVN::Pool->new,
	                      auth_provider_callbacks => $callbacks);
	$RA = bless $self, $class;

	# Make sure its canonicalized
	$self->url($url);
	$self->{svn_path} = $url;
	$self->{repos_root} = $self->get_repos_root;
	$self->{svn_path} =~ s#^\Q$self->{repos_root}\E(/|$)##;
	$self->{cache} = { check_path => { r => 0, data => {} },
	                   get_dir => { r => 0, data => {} } };

	return $RA;
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
	my ($d, undef, $props);

	if (::compare_svn_version('1.4.0') >= 0) {
		# n.b. in addition to being potentially more efficient,
		# this works around what appears to be a bug in some
		# SVN 1.8 versions
		my $kind = 1; # SVN_DIRENT_KIND
		($d, undef, $props) = $self->get_dir2($dir, $r, $kind, $pool);
	} else {
		($d, undef, $props) = $self->SUPER::get_dir($dir, $r, $pool);
	}
	my %dirents = map { $_ => { kind => $d->{$_}->kind } } keys %$d;
	$pool->clear;
	if ($r != $cache->{r}) {
		%{$cache->{data}} = ();
		$cache->{r} = $r;
	}
	$cache->{data}->{$dir} = [ \%dirents, $r, $props ];
	wantarray ? (\%dirents, $r, $props) : \%dirents;
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
				$s{'copyfrom_path'} = canonicalize_path($s{'copyfrom_path'});
			}
			$_[0]{$p} = \%s;
		}
		&$receiver(@_);
	});


	# the limit parameter was not supported in SVN 1.1.x, so we
	# drop it.  Therefore, the receiver callback passed to it
	# is made aware of this limitation by being wrapped if
	# the limit passed to is being wrapped.
	if (::compare_svn_version('1.2.0') <= 0) {
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

# uncommon, only for ancient SVN (<= 1.4.2)
sub trees_match {
	require IO::File;
	require SVN::Client;
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

	my @lock = (::compare_svn_version('1.2.0') >= 0) ? (undef, 0) : ();
	$self->SUPER::get_commit_editor($log, $cb, @lock, $pool);
}

sub gs_do_update {
	my ($self, $rev_a, $rev_b, $gs, $editor) = @_;
	my $new = ($rev_a == $rev_b);
	my $path = $gs->path;

	if ($new && -e $gs->{index}) {
		unlink $gs->{index} or die
		  "Couldn't unlink index: $gs->{index}: $!\n";
	}
	my $pool = SVN::Pool->new;
	$editor->set_path_strip($path);
	my (@pc) = split m#/#, $path;
	my $reporter = $self->do_update($rev_b, (@pc ? shift @pc : ''),
	                                1, $editor, $pool);
	my @lock = (::compare_svn_version('1.2.0') >= 0) ? (undef) : ();

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
	my $path = $gs->path;
	my $pool = SVN::Pool->new;

	my $old_url = $self->url;
	my $full_url = add_path_to_url( $self->url, $path );
	my ($ra, $reparented);

	if ($old_url =~ m#^svn(\+\w+)?://# ||
	    ($full_url =~ m#^https?://# &&
	     canonicalize_url($full_url) ne $full_url)) {
		$_[0] = undef;
		$self = undef;
		$RA = undef;
		$ra = Git::SVN::Ra->new($full_url);
		$ra_invalid = 1;
	} elsif ($old_url ne $full_url) {
		SVN::_Ra::svn_ra_reparent(
			$self->{session},
			canonicalize_url($full_url),
			$pool
		);
		$self->url($full_url);
		$reparented = 1;
	}

	$ra ||= $self;
	$url_b = canonicalize_url($url_b);
	my $reporter = $ra->do_switch($rev_b, '', 1, $url_b, $editor, $pool);
	my @lock = (::compare_svn_version('1.2.0') >= 0) ? (undef) : ();
	$reporter->set_path('', $rev_a, 0, @lock, $pool);
	$reporter->finish_report($pool);

	if ($reparented) {
		SVN::_Ra::svn_ra_reparent($self->{session}, $old_url, $pool);
		$self->url($old_url);
	}

	$pool->clear;
	$editor->{git_commit_ok};
}

sub longest_common_path {
	my ($gsv, $globs) = @_;
	my %common;
	my $common_max = scalar @$gsv;

	foreach my $gs (@$gsv) {
		my @tmp = split m#/#, $gs->path;
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
	# Make sure the cat_blob open2 FileHandle is created before calling
	# SVN::Pool::new_default so that it does not incorrectly end up in the pool.
	$::_repository->_open_cat_blob_if_needed;
	my $gpool = SVN::Pool->new_default;
	my $ra_url = $self->url;
	my $reload_ra = sub {
		$_[0] = undef;
		$self = undef;
		$RA = undef;
		$gpool->clear;
		$self = Git::SVN::Ra->new($ra_url);
		$ra_invalid = undef;
	};
	my $inc = $_log_window_size;
	my ($min, $max) = ($base, $head < $base + $inc ? $head : $base + $inc);
	my $longest_path = longest_common_path($gsv, $globs);
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

		my %exists = map { $_->path => $_ } @$gsv;
		foreach my $r (sort {$a <=> $b} keys %revs) {
			my ($paths, $logged) = @{delete $revs{$r}};

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
				$Git::SVN::INDEX_FILES{$gs->{index}} = 1;
			}
			foreach my $g (@$globs) {
				my $k = "svn-remote.$g->{remote}." .
				        "$g->{t}-maxRev";
				Git::SVN::tmp_config($k, $r);
			}
			$reload_ra->() if $ra_invalid;
		}
		# pre-fill the .rev_db since it'll eventually get filled in
		# with '0' x $oid_length if something new gets committed
		foreach my $gs (@$gsv) {
			next if $gs->rev_map_max >= $max;
			next if defined $gs->rev_map_get($max);
			$gs->rev_map_set($max, 0 x $::oid_length);
		}
		foreach my $g (@$globs) {
			my $k = "svn-remote.$g->{remote}.$g->{t}-maxRev";
			Git::SVN::tmp_config($k, $max);
		}
		last if $max >= $head;
		$min = $max + 1;
		$max += $inc;
		$max = $head if ($max > $head);

		$reload_ra->();
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
			$exists->{$p} = Git::SVN->init($self->url, $p, undef,
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
			                      $self->url, $pathname, undef,
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
	return $self->url if ($self->url eq $self->{repos_root});
	my $url = $self->{repos_root};
	my @components = split(m!/!, $self->{svn_path});
	my $c = '';
	do {
		$url = add_path_to_url($url, $c);
		eval {
			my $ra = (ref $self)->new($url);
			my $latest = $ra->get_latest_revnum;
			$ra->get_log("", $latest, 0, 1, 0, 1, sub {});
		};
	} while ($@ && defined($c = shift @components));

	return canonicalize_url($url);
}

sub can_do_switch {
	my $self = shift;
	unless (defined $can_do_switch) {
		my $pool = SVN::Pool->new;
		my $rep = eval {
			$self->do_switch(1, '', 0, $self->url,
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

1;
__END__

=head1 NAME

Git::SVN::Ra - Subversion remote access functions for git-svn

=head1 SYNOPSIS

    use Git::SVN::Ra;

    my $ra = Git::SVN::Ra->new($branchurl);
    my ($dirents, $fetched_revnum, $props) =
        $ra->get_dir('.', $SVN::Core::INVALID_REVNUM);

=head1 DESCRIPTION

This is a wrapper around the L<SVN::Ra> module for use by B<git-svn>.
It fills in some default parameters (such as the authentication
scheme), smooths over incompatibilities between libsvn versions, adds
caching, and implements some functions specific to B<git-svn>.

Do not use it unless you are developing git-svn.  The interface will
change as git-svn evolves.

=head1 DEPENDENCIES

Subversion perl bindings,
L<Git::SVN>.

C<Git::SVN::Ra> has not been tested using callers other than
B<git-svn> itself.

=head1 SEE ALSO

L<SVN::Ra>.

=head1 INCOMPATIBILITIES

None reported.

=head1 BUGS

None.
