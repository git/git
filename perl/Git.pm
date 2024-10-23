=head1 NAME

Git - Perl interface to the Git version control system

=cut


package Git;

require v5.26;
use strict;
use warnings $ENV{GIT_PERL_FATAL_WARNINGS} ? qw(FATAL all) : ();

BEGIN {

our ($VERSION, @ISA, @EXPORT, @EXPORT_OK);

# Totally unstable API.
$VERSION = '0.01';


=head1 SYNOPSIS

  use Git;

  my $version = Git::command_oneline('version');

  git_cmd_try { Git::command_noisy('update-server-info') }
              '%s failed w/ code %d';

  my $repo = Git->repository (Directory => '/srv/git/cogito.git');


  my @revs = $repo->command('rev-list', '--since=last monday', '--all');

  my ($fh, $c) = $repo->command_output_pipe('rev-list', '--since=last monday', '--all');
  my $lastrev = <$fh>; chomp $lastrev;
  $repo->command_close_pipe($fh, $c);

  my $lastrev = $repo->command_oneline( [ 'rev-list', '--all' ],
                                        STDERR => 0 );

  my $sha1 = $repo->hash_and_insert_object('file.txt');
  my $tempfile = tempfile();
  my $size = $repo->cat_blob($sha1, $tempfile);

=cut


require Exporter;

@ISA = qw(Exporter);

@EXPORT = qw(git_cmd_try);

# Methods which can be called as standalone functions as well:
@EXPORT_OK = qw(command command_oneline command_noisy
                command_output_pipe command_input_pipe command_close_pipe
                command_bidi_pipe command_close_bidi_pipe
                version exec_path html_path hash_object git_cmd_try
                remote_refs prompt
                get_tz_offset get_record
                credential credential_read credential_write
                temp_acquire temp_is_locked temp_release temp_reset temp_path
                unquote_path);


=head1 DESCRIPTION

This module provides Perl scripts easy way to interface the Git version control
system. The modules have an easy and well-tested way to call arbitrary Git
commands; in the future, the interface will also provide specialized methods
for doing easily operations which are not totally trivial to do over
the generic command interface.

While some commands can be executed outside of any context (e.g. 'version'
or 'init'), most operations require a repository context, which in practice
means getting an instance of the Git object using the repository() constructor.
(In the future, we will also get a new_repository() constructor.) All commands
called as methods of the object are then executed in the context of the
repository.

Part of the "repository state" is also information about path to the attached
working copy (unless you work with a bare repository). You can also navigate
inside of the working copy using the C<wc_chdir()> method. (Note that
the repository object is self-contained and will not change working directory
of your process.)

TODO: In the future, we might also do

	my $remoterepo = $repo->remote_repository (Name => 'cogito', Branch => 'master');
	$remoterepo ||= Git->remote_repository ('http://git.or.cz/cogito.git/');
	my @refs = $remoterepo->refs();

Currently, the module merely wraps calls to external Git tools. In the future,
it will provide a much faster way to interact with Git by linking directly
to libgit. This should be completely opaque to the user, though (performance
increase notwithstanding).

=cut


sub carp { require Carp; goto &Carp::carp }
sub croak { require Carp; goto &Carp::croak }
use Git::LoadCPAN::Error qw(:try);
}


=head1 CONSTRUCTORS

=over 4

=item repository ( OPTIONS )

=item repository ( DIRECTORY )

=item repository ()

Construct a new repository object.
C<OPTIONS> are passed in a hash like fashion, using key and value pairs.
Possible options are:

B<Repository> - Path to the Git repository.

B<WorkingCopy> - Path to the associated working copy; not strictly required
as many commands will happily crunch on a bare repository.

B<WorkingSubdir> - Subdirectory in the working copy to work inside.
Just left undefined if you do not want to limit the scope of operations.

B<Directory> - Path to the Git working directory in its usual setup.
The C<.git> directory is searched in the directory and all the parent
directories; if found, C<WorkingCopy> is set to the directory containing
it and C<Repository> to the C<.git> directory itself. If no C<.git>
directory was found, the C<Directory> is assumed to be a bare repository,
C<Repository> is set to point at it and C<WorkingCopy> is left undefined.
If the C<$GIT_DIR> environment variable is set, things behave as expected
as well.

You should not use both C<Directory> and either of C<Repository> and
C<WorkingCopy> - the results of that are undefined.

Alternatively, a directory path may be passed as a single scalar argument
to the constructor; it is equivalent to setting only the C<Directory> option
field.

Calling the constructor with no options whatsoever is equivalent to
calling it with C<< Directory => '.' >>. In general, if you are building
a standard porcelain command, simply doing C<< Git->repository() >> should
do the right thing and setup the object to reflect exactly where the user
is right now.

=cut

sub repository {
	my $class = shift;
	my @args = @_;
	my %opts = ();
	my $self;

	if (defined $args[0]) {
		if ($#args % 2 != 1) {
			# Not a hash.
			$#args == 0 or throw Error::Simple("bad usage");
			%opts = ( Directory => $args[0] );
		} else {
			%opts = @args;
		}
	}

	if (not defined $opts{Repository} and not defined $opts{WorkingCopy}
		and not defined $opts{Directory}) {
		$opts{Directory} = '.';
	}

	if (defined $opts{Directory}) {
		-d $opts{Directory} or throw Error::Simple("Directory not found: $opts{Directory} $!");

		my $search = Git->repository(WorkingCopy => $opts{Directory});

		# This rev-parse will throw an exception if we're not in a
		# repository, which is what we want, but it's kind of noisy.
		# Ideally we'd capture stderr and relay it, but doing so is
		# awkward without depending on it fitting in a pipe buffer. So
		# we just reproduce a plausible error message ourselves.
		my $out;
		try {
		  # Note that "--is-bare-repository" must come first, as
		  # --git-dir output could contain newlines.
		  $out = $search->command([qw(rev-parse --is-bare-repository --absolute-git-dir)],
			                  STDERR => 0);
		} catch Git::Error::Command with {
			throw Error::Simple("fatal: not a git repository: $opts{Directory}");
		};

		chomp $out;
		my ($bare, $dir) = split /\n/, $out, 2;

		# We know this is an absolute path, because we used
		# --absolute-git-dir above.
		$opts{Repository} = $dir;

		if ($bare ne 'true') {
			require Cwd;
			# If --git-dir went ok, this shouldn't die either.
			my $prefix = $search->command_oneline('rev-parse', '--show-prefix');
			$dir = Cwd::abs_path($opts{Directory}) . '/';
			if ($prefix) {
				if (substr($dir, -length($prefix)) ne $prefix) {
					throw Error::Simple("rev-parse confused me - $dir does not have trailing $prefix");
				}
				substr($dir, -length($prefix)) = '';
			}
			$opts{WorkingCopy} = $dir;
			$opts{WorkingSubdir} = $prefix;

		}

		delete $opts{Directory};
	}

	$self = { opts => \%opts };
	bless $self, $class;
}

=back

=head1 METHODS

=over 4

=item command ( COMMAND [, ARGUMENTS... ] )

=item command ( [ COMMAND, ARGUMENTS... ], { Opt => Val ... } )

Execute the given Git C<COMMAND> (specify it without the 'git-'
prefix), optionally with the specified extra C<ARGUMENTS>.

The second more elaborate form can be used if you want to further adjust
the command execution. Currently, only one option is supported:

B<STDERR> - How to deal with the command's error output. By default (C<undef>)
it is delivered to the caller's C<STDERR>. A false value (0 or '') will cause
it to be thrown away. If you want to process it, you can get it in a filehandle
you specify, but you must be extremely careful; if the error output is not
very short and you want to read it in the same process as where you called
C<command()>, you are set up for a nice deadlock!

The method can be called without any instance or on a specified Git repository
(in that case the command will be run in the repository context).

In scalar context, it returns all the command output in a single string
(verbatim).

In array context, it returns an array containing lines printed to the
command's stdout (without trailing newlines).

In both cases, the command's stdin and stderr are the same as the caller's.

=cut

sub command {
	my ($fh, $ctx) = command_output_pipe(@_);

	if (not defined wantarray) {
		# Nothing to pepper the possible exception with.
		_cmd_close($ctx, $fh);

	} elsif (not wantarray) {
		local $/;
		my $text = <$fh>;
		try {
			_cmd_close($ctx, $fh);
		} catch Git::Error::Command with {
			# Pepper with the output:
			my $E = shift;
			$E->{'-outputref'} = \$text;
			throw $E;
		};
		return $text;

	} else {
		my @lines = <$fh>;
		defined and chomp for @lines;
		try {
			_cmd_close($ctx, $fh);
		} catch Git::Error::Command with {
			my $E = shift;
			$E->{'-outputref'} = \@lines;
			throw $E;
		};
		return @lines;
	}
}


=item command_oneline ( COMMAND [, ARGUMENTS... ] )

=item command_oneline ( [ COMMAND, ARGUMENTS... ], { Opt => Val ... } )

Execute the given C<COMMAND> in the same way as command()
does but always return a scalar string containing the first line
of the command's standard output.

=cut

sub command_oneline {
	my ($fh, $ctx) = command_output_pipe(@_);

	my $line = <$fh>;
	defined $line and chomp $line;
	try {
		_cmd_close($ctx, $fh);
	} catch Git::Error::Command with {
		# Pepper with the output:
		my $E = shift;
		$E->{'-outputref'} = \$line;
		throw $E;
	};
	return $line;
}


=item command_output_pipe ( COMMAND [, ARGUMENTS... ] )

=item command_output_pipe ( [ COMMAND, ARGUMENTS... ], { Opt => Val ... } )

Execute the given C<COMMAND> in the same way as command()
does but return a pipe filehandle from which the command output can be
read.

The function can return C<($pipe, $ctx)> in array context.
See C<command_close_pipe()> for details.

=cut

sub command_output_pipe {
	_command_common_pipe('-|', @_);
}


=item command_input_pipe ( COMMAND [, ARGUMENTS... ] )

=item command_input_pipe ( [ COMMAND, ARGUMENTS... ], { Opt => Val ... } )

Execute the given C<COMMAND> in the same way as command_output_pipe()
does but return an input pipe filehandle instead; the command output
is not captured.

The function can return C<($pipe, $ctx)> in array context.
See C<command_close_pipe()> for details.

=cut

sub command_input_pipe {
	_command_common_pipe('|-', @_);
}


=item command_close_pipe ( PIPE [, CTX ] )

Close the C<PIPE> as returned from C<command_*_pipe()>, checking
whether the command finished successfully. The optional C<CTX> argument
is required if you want to see the command name in the error message,
and it is the second value returned by C<command_*_pipe()> when
called in array context. The call idiom is:

	my ($fh, $ctx) = $r->command_output_pipe('status');
	while (<$fh>) { ... }
	$r->command_close_pipe($fh, $ctx);

Note that you should not rely on whatever actually is in C<CTX>;
currently it is simply the command name but in future the context might
have more complicated structure.

=cut

sub command_close_pipe {
	my ($self, $fh, $ctx) = _maybe_self(@_);
	$ctx ||= '<unknown>';
	_cmd_close($ctx, $fh);
}

=item command_bidi_pipe ( COMMAND [, ARGUMENTS... ] )

Execute the given C<COMMAND> in the same way as command_output_pipe()
does but return both an input pipe filehandle and an output pipe filehandle.

The function will return C<($pid, $pipe_in, $pipe_out, $ctx)>.
See C<command_close_bidi_pipe()> for details.

=cut

sub command_bidi_pipe {
	my ($pid, $in, $out);
	my ($self) = _maybe_self(@_);
	local %ENV = %ENV;
	my $cwd_save = undef;
	if ($self) {
		shift;
		require Cwd;
		$cwd_save = Cwd::getcwd();
		_setup_git_cmd_env($self);
	}
	require IPC::Open2;
	$pid = IPC::Open2::open2($in, $out, 'git', @_);
	chdir($cwd_save) if $cwd_save;
	return ($pid, $in, $out, join(' ', @_));
}

=item command_close_bidi_pipe ( PID, PIPE_IN, PIPE_OUT [, CTX] )

Close the C<PIPE_IN> and C<PIPE_OUT> as returned from C<command_bidi_pipe()>,
checking whether the command finished successfully. The optional C<CTX>
argument is required if you want to see the command name in the error message,
and it is the fourth value returned by C<command_bidi_pipe()>.  The call idiom
is:

	my ($pid, $in, $out, $ctx) = $r->command_bidi_pipe(qw(cat-file --batch-check));
	print $out "000000000\n";
	while (<$in>) { ... }
	$r->command_close_bidi_pipe($pid, $in, $out, $ctx);

Note that you should not rely on whatever actually is in C<CTX>;
currently it is simply the command name but in future the context might
have more complicated structure.

C<PIPE_IN> and C<PIPE_OUT> may be C<undef> if they have been closed prior to
calling this function.  This may be useful in a query-response type of
commands where caller first writes a query and later reads response, eg:

	my ($pid, $in, $out, $ctx) = $r->command_bidi_pipe(qw(cat-file --batch-check));
	print $out "000000000\n";
	close $out;
	while (<$in>) { ... }
	$r->command_close_bidi_pipe($pid, $in, undef, $ctx);

This idiom may prevent potential dead locks caused by data sent to the output
pipe not being flushed and thus not reaching the executed command.

=cut

sub command_close_bidi_pipe {
	local $?;
	my ($self, $pid, $in, $out, $ctx) = _maybe_self(@_);
	_cmd_close($ctx, (grep { defined } ($in, $out)));
	waitpid $pid, 0;
	if ($? >> 8) {
		throw Git::Error::Command($ctx, $? >>8);
	}
}


=item command_noisy ( COMMAND [, ARGUMENTS... ] )

Execute the given C<COMMAND> in the same way as command() does but do not
capture the command output - the standard output is not redirected and goes
to the standard output of the caller application.

While the method is called command_noisy(), you might want to as well use
it for the most silent Git commands which you know will never pollute your
stdout but you want to avoid the overhead of the pipe setup when calling them.

The function returns only after the command has finished running.

=cut

sub command_noisy {
	my ($self, $cmd, @args) = _maybe_self(@_);
	_check_valid_cmd($cmd);

	my $pid = fork;
	if (not defined $pid) {
		throw Error::Simple("fork failed: $!");
	} elsif ($pid == 0) {
		_cmd_exec($self, $cmd, @args);
	}
	if (waitpid($pid, 0) > 0 and $?>>8 != 0) {
		throw Git::Error::Command(join(' ', $cmd, @args), $? >> 8);
	}
}


=item version ()

Return the Git version in use.

=cut

sub version {
	my $verstr = command_oneline('--version');
	$verstr =~ s/^git version //;
	$verstr;
}


=item exec_path ()

Return path to the Git sub-command executables (the same as
C<git --exec-path>). Useful mostly only internally.

=cut

sub exec_path { command_oneline('--exec-path') }


=item html_path ()

Return path to the Git html documentation (the same as
C<git --html-path>). Useful mostly only internally.

=cut

sub html_path { command_oneline('--html-path') }


=item get_tz_offset ( TIME )

Return the time zone offset from GMT in the form +/-HHMM where HH is
the number of hours from GMT and MM is the number of minutes.  This is
the equivalent of what strftime("%z", ...) would provide on a GNU
platform.

If TIME is not supplied, the current local time is used.

=cut

sub get_tz_offset {
	# some systems don't handle or mishandle %z, so be creative.
	my $t = shift || time;
	my @t = localtime($t);
	$t[5] += 1900;
	require Time::Local;
	my $gm = Time::Local::timegm(@t);
	my $sign = qw( + + - )[ $gm <=> $t ];
	return sprintf("%s%02d%02d", $sign, (gmtime(abs($t - $gm)))[2,1]);
}

=item get_record ( FILEHANDLE, INPUT_RECORD_SEPARATOR )

Read one record from FILEHANDLE delimited by INPUT_RECORD_SEPARATOR,
removing any trailing INPUT_RECORD_SEPARATOR.

=cut

sub get_record {
	my ($fh, $rs) = @_;
	local $/ = $rs;
	my $rec = <$fh>;
	chomp $rec if defined $rec;
	$rec;
}

=item prompt ( PROMPT , ISPASSWORD  )

Query user C<PROMPT> and return answer from user.

Honours GIT_ASKPASS and SSH_ASKPASS environment variables for querying
the user. If no *_ASKPASS variable is set or an error occurred,
the terminal is tried as a fallback.
If C<ISPASSWORD> is set and true, the terminal disables echo.

=cut

sub prompt {
	my ($prompt, $isPassword) = @_;
	my $ret;
	if (exists $ENV{'GIT_ASKPASS'}) {
		$ret = _prompt($ENV{'GIT_ASKPASS'}, $prompt);
	}
	if (!defined $ret && exists $ENV{'SSH_ASKPASS'}) {
		$ret = _prompt($ENV{'SSH_ASKPASS'}, $prompt);
	}
	if (!defined $ret) {
		print STDERR $prompt;
		STDERR->flush;
		if (defined $isPassword && $isPassword) {
			require Term::ReadKey;
			Term::ReadKey::ReadMode('noecho');
			$ret = '';
			while (defined(my $key = Term::ReadKey::ReadKey(0))) {
				last if $key =~ /[\012\015]/; # \n\r
				$ret .= $key;
			}
			Term::ReadKey::ReadMode('restore');
			print STDERR "\n";
			STDERR->flush;
		} else {
			chomp($ret = <STDIN>);
		}
	}
	return $ret;
}

sub _prompt {
	my ($askpass, $prompt) = @_;
	return unless length $askpass;
	$prompt =~ s/\n/ /g;
	my $ret;
	open my $fh, "-|", $askpass, $prompt or return;
	$ret = <$fh>;
	$ret =~ s/[\015\012]//g; # strip \r\n, chomp does not work on all systems (i.e. windows) as expected
	close ($fh);
	return $ret;
}

=item repo_path ()

Return path to the git repository. Must be called on a repository instance.

=cut

sub repo_path { $_[0]->{opts}->{Repository} }


=item wc_path ()

Return path to the working copy. Must be called on a repository instance.

=cut

sub wc_path { $_[0]->{opts}->{WorkingCopy} }


=item wc_subdir ()

Return path to the subdirectory inside of a working copy. Must be called
on a repository instance.

=cut

sub wc_subdir { $_[0]->{opts}->{WorkingSubdir} ||= '' }


=item wc_chdir ( SUBDIR )

Change the working copy subdirectory to work within. The C<SUBDIR> is
relative to the working copy root directory (not the current subdirectory).
Must be called on a repository instance attached to a working copy
and the directory must exist.

=cut

sub wc_chdir {
	my ($self, $subdir) = @_;
	$self->wc_path()
		or throw Error::Simple("bare repository");

	-d $self->wc_path().'/'.$subdir
		or throw Error::Simple("subdir not found: $subdir $!");
	# Of course we will not "hold" the subdirectory so anyone
	# can delete it now and we will never know. But at least we tried.

	$self->{opts}->{WorkingSubdir} = $subdir;
}


=item config ( VARIABLE )

Retrieve the configuration C<VARIABLE> in the same manner as C<config>
does. In scalar context requires the variable to be set only one time
(exception is thrown otherwise), in array context returns allows the
variable to be set multiple times and returns all the values.

=cut

sub config {
	return _config_common({}, @_);
}


=item config_bool ( VARIABLE )

Retrieve the bool configuration C<VARIABLE>. The return value
is usable as a boolean in perl (and C<undef> if it's not defined,
of course).

=cut

sub config_bool {
	my $val = scalar _config_common({'kind' => '--bool'}, @_);

	# Do not rewrite this as return (defined $val && $val eq 'true')
	# as some callers do care what kind of falsehood they receive.
	if (!defined $val) {
		return undef;
	} else {
		return $val eq 'true';
	}
}


=item config_path ( VARIABLE )

Retrieve the path configuration C<VARIABLE>. The return value
is an expanded path or C<undef> if it's not defined.

=cut

sub config_path {
	return _config_common({'kind' => '--path'}, @_);
}


=item config_int ( VARIABLE )

Retrieve the integer configuration C<VARIABLE>. The return value
is simple decimal number.  An optional value suffix of 'k', 'm',
or 'g' in the config file will cause the value to be multiplied
by 1024, 1048576 (1024^2), or 1073741824 (1024^3) prior to output.
It would return C<undef> if configuration variable is not defined.

=cut

sub config_int {
	return scalar _config_common({'kind' => '--int'}, @_);
}

=item config_regexp ( RE )

Retrieve the list of configuration key names matching the regular
expression C<RE>. The return value is a list of strings matching
this regex.

=cut

sub config_regexp {
	my ($self, $regex) = _maybe_self(@_);
	try {
		my @cmd = ('config', '--name-only', '--get-regexp', $regex);
		unshift @cmd, $self if $self;
		my @matches = command(@cmd);
		return @matches;
	} catch Git::Error::Command with {
		my $E = shift;
		if ($E->value() == 1) {
			my @matches = ();
			return @matches;
		} else {
			throw $E;
		}
	};
}

# Common subroutine to implement bulk of what the config* family of methods
# do. This currently wraps command('config') so it is not so fast.
sub _config_common {
	my ($opts) = shift @_;
	my ($self, $var) = _maybe_self(@_);

	try {
		my @cmd = ('config', $opts->{'kind'} ? $opts->{'kind'} : ());
		unshift @cmd, $self if $self;
		if (wantarray) {
			return command(@cmd, '--get-all', $var);
		} else {
			return command_oneline(@cmd, '--get', $var);
		}
	} catch Git::Error::Command with {
		my $E = shift;
		if ($E->value() == 1) {
			# Key not found.
			return;
		} else {
			throw $E;
		}
	};
}

=item get_colorbool ( NAME )

Finds if color should be used for NAMEd operation from the configuration,
and returns boolean (true for "use color", false for "do not use color").

=cut

sub get_colorbool {
	my ($self, $var) = @_;
	my $stdout_to_tty = (-t STDOUT) ? "true" : "false";
	my $use_color = $self->command_oneline('config', '--get-colorbool',
					       $var, $stdout_to_tty);
	return ($use_color eq 'true');
}

=item get_color ( SLOT, COLOR )

Finds color for SLOT from the configuration, while defaulting to COLOR,
and returns the ANSI color escape sequence:

	print $repo->get_color("color.interactive.prompt", "underline blue white");
	print "some text";
	print $repo->get_color("", "normal");

=cut

sub get_color {
	my ($self, $slot, $default) = @_;
	my $color = $self->command_oneline('config', '--get-color', $slot, $default);
	if (!defined $color) {
		$color = "";
	}
	return $color;
}

=item remote_refs ( REPOSITORY [, GROUPS [, REFGLOBS ] ] )

This function returns a hashref of refs stored in a given remote repository.
The hash is in the format C<refname =\> hash>. For tags, the C<refname> entry
contains the tag object while a C<refname^{}> entry gives the tagged objects.

C<REPOSITORY> has the same meaning as the appropriate C<git-ls-remote>
argument; either a URL or a remote name (if called on a repository instance).
C<GROUPS> is an optional arrayref that can contain 'tags' to return all the
tags and/or 'heads' to return all the heads. C<REFGLOB> is an optional array
of strings containing a shell-like glob to further limit the refs returned in
the hash; the meaning is again the same as the appropriate C<git-ls-remote>
argument.

This function may or may not be called on a repository instance. In the former
case, remote names as defined in the repository are recognized as repository
specifiers.

=cut

sub remote_refs {
	my ($self, $repo, $groups, $refglobs) = _maybe_self(@_);
	my @args;
	if (ref $groups eq 'ARRAY') {
		foreach (@$groups) {
			if ($_ eq 'heads') {
				push (@args, '--heads');
			} elsif ($_ eq 'tags') {
				push (@args, '--tags');
			} else {
				# Ignore unknown groups for future
				# compatibility
			}
		}
	}
	push (@args, $repo);
	if (ref $refglobs eq 'ARRAY') {
		push (@args, @$refglobs);
	}

	my @self = $self ? ($self) : (); # Ultra trickery
	my ($fh, $ctx) = Git::command_output_pipe(@self, 'ls-remote', @args);
	my %refs;
	while (<$fh>) {
		chomp;
		my ($hash, $ref) = split(/\t/, $_, 2);
		$refs{$ref} = $hash;
	}
	Git::command_close_pipe(@self, $fh, $ctx);
	return \%refs;
}


=item ident ( TYPE | IDENTSTR )

=item ident_person ( TYPE | IDENTSTR | IDENTARRAY )

This suite of functions retrieves and parses ident information, as stored
in the commit and tag objects or produced by C<var GIT_type_IDENT> (thus
C<TYPE> can be either I<author> or I<committer>; case is insignificant).

The C<ident> method retrieves the ident information from C<git var>
and either returns it as a scalar string or as an array with the fields parsed.
Alternatively, it can take a prepared ident string (e.g. from the commit
object) and just parse it.

C<ident_person> returns the person part of the ident - name and email;
it can take the same arguments as C<ident> or the array returned by C<ident>.

The synopsis is like:

	my ($name, $email, $time_tz) = ident('author');
	"$name <$email>" eq ident_person('author');
	"$name <$email>" eq ident_person($name);
	$time_tz =~ /^\d+ [+-]\d{4}$/;

=cut

sub ident {
	my ($self, $type) = _maybe_self(@_);
	my $identstr;
	if (lc $type eq lc 'committer' or lc $type eq lc 'author') {
		my @cmd = ('var', 'GIT_'.uc($type).'_IDENT');
		unshift @cmd, $self if $self;
		$identstr = command_oneline(@cmd);
	} else {
		$identstr = $type;
	}
	if (wantarray) {
		return $identstr =~ /^(.*) <(.*)> (\d+ [+-]\d{4})$/;
	} else {
		return $identstr;
	}
}

sub ident_person {
	my ($self, @ident) = _maybe_self(@_);
	$#ident == 0 and @ident = $self ? $self->ident($ident[0]) : ident($ident[0]);
	return "$ident[0] <$ident[1]>";
}

=item hash_object ( TYPE, FILENAME )

Compute the SHA1 object id of the given C<FILENAME> considering it is
of the C<TYPE> object type (C<blob>, C<commit>, C<tree>).

The method can be called without any instance or on a specified Git repository,
it makes zero difference.

The function returns the SHA1 hash.

=cut

# TODO: Support for passing FILEHANDLE instead of FILENAME
sub hash_object {
	my ($self, $type, $file) = _maybe_self(@_);
	command_oneline('hash-object', '-t', $type, $file);
}


=item hash_and_insert_object ( FILENAME )

Compute the SHA1 object id of the given C<FILENAME> and add the object to the
object database.

The function returns the SHA1 hash.

=cut

# TODO: Support for passing FILEHANDLE instead of FILENAME
sub hash_and_insert_object {
	my ($self, $filename) = @_;

	carp "Bad filename \"$filename\"" if $filename =~ /[\r\n]/;

	$self->_open_hash_and_insert_object_if_needed();
	my ($in, $out) = ($self->{hash_object_in}, $self->{hash_object_out});

	unless (print $out $filename, "\n") {
		$self->_close_hash_and_insert_object();
		throw Error::Simple("out pipe went bad");
	}

	chomp(my $hash = <$in>);
	unless (defined($hash)) {
		$self->_close_hash_and_insert_object();
		throw Error::Simple("in pipe went bad");
	}

	return $hash;
}

sub _open_hash_and_insert_object_if_needed {
	my ($self) = @_;

	return if defined($self->{hash_object_pid});

	($self->{hash_object_pid}, $self->{hash_object_in},
	 $self->{hash_object_out}, $self->{hash_object_ctx}) =
		$self->command_bidi_pipe(qw(hash-object -w --stdin-paths --no-filters));
}

sub _close_hash_and_insert_object {
	my ($self) = @_;

	return unless defined($self->{hash_object_pid});

	my @vars = map { 'hash_object_' . $_ } qw(pid in out ctx);

	command_close_bidi_pipe(@$self{@vars});
	delete @$self{@vars};
}

=item cat_blob ( SHA1, FILEHANDLE )

Prints the contents of the blob identified by C<SHA1> to C<FILEHANDLE> and
returns the number of bytes printed.

=cut

sub cat_blob {
	my ($self, $sha1, $fh) = @_;

	$self->_open_cat_blob_if_needed();
	my ($in, $out) = ($self->{cat_blob_in}, $self->{cat_blob_out});

	unless (print $out $sha1, "\n") {
		$self->_close_cat_blob();
		throw Error::Simple("out pipe went bad");
	}

	my $description = <$in>;
	if ($description =~ / missing$/) {
		carp "$sha1 doesn't exist in the repository";
		return -1;
	}

	if ($description !~ /^[0-9a-fA-F]{40}(?:[0-9a-fA-F]{24})? \S+ (\d+)$/) {
		carp "Unexpected result returned from git cat-file";
		return -1;
	}

	my $size = $1;

	my $blob;
	my $bytesLeft = $size;

	while (1) {
		last unless $bytesLeft;

		my $bytesToRead = $bytesLeft < 1024 ? $bytesLeft : 1024;
		my $read = read($in, $blob, $bytesToRead);
		unless (defined($read)) {
			$self->_close_cat_blob();
			throw Error::Simple("in pipe went bad");
		}
		unless (print $fh $blob) {
			$self->_close_cat_blob();
			throw Error::Simple("couldn't write to passed in filehandle");
		}
		$bytesLeft -= $read;
	}

	# Skip past the trailing newline.
	my $newline;
	my $read = read($in, $newline, 1);
	unless (defined($read)) {
		$self->_close_cat_blob();
		throw Error::Simple("in pipe went bad");
	}
	unless ($read == 1 && $newline eq "\n") {
		$self->_close_cat_blob();
		throw Error::Simple("didn't find newline after blob");
	}

	return $size;
}

sub _open_cat_blob_if_needed {
	my ($self) = @_;

	return if defined($self->{cat_blob_pid});

	($self->{cat_blob_pid}, $self->{cat_blob_in},
	 $self->{cat_blob_out}, $self->{cat_blob_ctx}) =
		$self->command_bidi_pipe(qw(cat-file --batch));
}

sub _close_cat_blob {
	my ($self) = @_;

	return unless defined($self->{cat_blob_pid});

	my @vars = map { 'cat_blob_' . $_ } qw(pid in out ctx);

	command_close_bidi_pipe(@$self{@vars});
	delete @$self{@vars};
}


=item credential_read( FILEHANDLE )

Reads credential key-value pairs from C<FILEHANDLE>.  Reading stops at EOF or
when an empty line is encountered.  Each line must be of the form C<key=value>
with a non-empty key.  Function returns hash with all read values.  Any white
space (other than new-line character) is preserved.

=cut

sub credential_read {
	my ($self, $reader) = _maybe_self(@_);
	my %credential;
	while (<$reader>) {
		chomp;
		if ($_ eq '') {
			last;
		} elsif (!/^([^=]+)=(.*)$/) {
			throw Error::Simple("unable to parse git credential data:\n$_");
		}
		$credential{$1} = $2;
	}
	return %credential;
}

=item credential_write( FILEHANDLE, CREDENTIAL_HASHREF )

Writes credential key-value pairs from hash referenced by
C<CREDENTIAL_HASHREF> to C<FILEHANDLE>.  Keys and values cannot contain
new-lines or NUL bytes characters, and key cannot contain equal signs nor be
empty (if they do Error::Simple is thrown).  Any white space is preserved.  If
value for a key is C<undef>, it will be skipped.

If C<'url'> key exists it will be written first.  (All the other key-value
pairs are written in sorted order but you should not depend on that).  Once
all lines are written, an empty line is printed.

=cut

sub credential_write {
	my ($self, $writer, $credential) = _maybe_self(@_);
	my ($key, $value);

	# Check if $credential is valid prior to writing anything
	while (($key, $value) = each %$credential) {
		if (!defined $key || !length $key) {
			throw Error::Simple("credential key empty or undefined");
		} elsif ($key =~ /[=\n\0]/) {
			throw Error::Simple("credential key contains invalid characters: $key");
		} elsif (defined $value && $value =~ /[\n\0]/) {
			throw Error::Simple("credential value for key=$key contains invalid characters: $value");
		}
	}

	for $key (sort {
		# url overwrites other fields, so it must come first
		return -1 if $a eq 'url';
		return  1 if $b eq 'url';
		return $a cmp $b;
	} keys %$credential) {
		if (defined $credential->{$key}) {
			print $writer $key, '=', $credential->{$key}, "\n";
		}
	}
	print $writer "\n";
}

sub _credential_run {
	my ($self, $credential, $op) = _maybe_self(@_);
	my ($pid, $reader, $writer, $ctx) = command_bidi_pipe('credential', $op);

	credential_write $writer, $credential;
	close $writer;

	if ($op eq "fill") {
		%$credential = credential_read $reader;
	}
	if (<$reader>) {
		throw Error::Simple("unexpected output from git credential $op response:\n$_\n");
	}

	command_close_bidi_pipe($pid, $reader, undef, $ctx);
}

=item credential( CREDENTIAL_HASHREF [, OPERATION ] )

=item credential( CREDENTIAL_HASHREF, CODE )

Executes C<git credential> for a given set of credentials and specified
operation.  In both forms C<CREDENTIAL_HASHREF> needs to be a reference to
a hash which stores credentials.  Under certain conditions the hash can
change.

In the first form, C<OPERATION> can be C<'fill'>, C<'approve'> or C<'reject'>,
and function will execute corresponding C<git credential> sub-command.  If
it's omitted C<'fill'> is assumed.  In case of C<'fill'> the values stored in
C<CREDENTIAL_HASHREF> will be changed to the ones returned by the C<git
credential fill> command.  The usual usage would look something like:

	my %cred = (
		'protocol' => 'https',
		'host' => 'example.com',
		'username' => 'bob'
	);
	Git::credential \%cred;
	if (try_to_authenticate($cred{'username'}, $cred{'password'})) {
		Git::credential \%cred, 'approve';
		... do more stuff ...
	} else {
		Git::credential \%cred, 'reject';
	}

In the second form, C<CODE> needs to be a reference to a subroutine.  The
function will execute C<git credential fill> to fill the provided credential
hash, then call C<CODE> with C<CREDENTIAL_HASHREF> as the sole argument.  If
C<CODE>'s return value is defined, the function will execute C<git credential
approve> (if return value yields true) or C<git credential reject> (if return
value is false).  If the return value is undef, nothing at all is executed;
this is useful, for example, if the credential could neither be verified nor
rejected due to an unrelated network error.  The return value is the same as
what C<CODE> returns.  With this form, the usage might look as follows:

	if (Git::credential {
		'protocol' => 'https',
		'host' => 'example.com',
		'username' => 'bob'
	}, sub {
		my $cred = shift;
		return !!try_to_authenticate($cred->{'username'},
		                             $cred->{'password'});
	}) {
		... do more stuff ...
	}

=cut

sub credential {
	my ($self, $credential, $op_or_code) = (_maybe_self(@_), 'fill');

	if ('CODE' eq ref $op_or_code) {
		_credential_run $credential, 'fill';
		my $ret = $op_or_code->($credential);
		if (defined $ret) {
			_credential_run $credential, $ret ? 'approve' : 'reject';
		}
		return $ret;
	} else {
		_credential_run $credential, $op_or_code;
	}
}

{ # %TEMP_* Lexical Context

my (%TEMP_FILEMAP, %TEMP_FILES);

=item temp_acquire ( NAME )

Attempts to retrieve the temporary file mapped to the string C<NAME>. If an
associated temp file has not been created this session or was closed, it is
created, cached, and set for autoflush and binmode.

Internally locks the file mapped to C<NAME>. This lock must be released with
C<temp_release()> when the temp file is no longer needed. Subsequent attempts
to retrieve temporary files mapped to the same C<NAME> while still locked will
cause an error. This locking mechanism provides a weak guarantee and is not
threadsafe. It does provide some error checking to help prevent temp file refs
writing over one another.

In general, the L<File::Handle> returned should not be closed by consumers as
it defeats the purpose of this caching mechanism. If you need to close the temp
file handle, then you should use L<File::Temp> or another temp file faculty
directly. If a handle is closed and then requested again, then a warning will
issue.

=cut

sub temp_acquire {
	my $temp_fd = _temp_cache(@_);

	$TEMP_FILES{$temp_fd}{locked} = 1;
	$temp_fd;
}

=item temp_is_locked ( NAME )

Returns true if the internal lock created by a previous C<temp_acquire()>
call with C<NAME> is still in effect.

When temp_acquire is called on a C<NAME>, it internally locks the temporary
file mapped to C<NAME>.  That lock will not be released until C<temp_release()>
is called with either the original C<NAME> or the L<File::Handle> that was
returned from the original call to temp_acquire.

Subsequent attempts to call C<temp_acquire()> with the same C<NAME> will fail
unless there has been an intervening C<temp_release()> call for that C<NAME>
(or its corresponding L<File::Handle> that was returned by the original
C<temp_acquire()> call).

If true is returned by C<temp_is_locked()> for a C<NAME>, an attempt to
C<temp_acquire()> the same C<NAME> will cause an error unless
C<temp_release> is first called on that C<NAME> (or its corresponding
L<File::Handle> that was returned by the original C<temp_acquire()> call).

=cut

sub temp_is_locked {
	my ($self, $name) = _maybe_self(@_);
	my $temp_fd = \$TEMP_FILEMAP{$name};

	defined $$temp_fd && $$temp_fd->opened && $TEMP_FILES{$$temp_fd}{locked};
}

=item temp_release ( NAME )

=item temp_release ( FILEHANDLE )

Releases a lock acquired through C<temp_acquire()>. Can be called either with
the C<NAME> mapping used when acquiring the temp file or with the C<FILEHANDLE>
referencing a locked temp file.

Warns if an attempt is made to release a file that is not locked.

The temp file will be truncated before being released. This can help to reduce
disk I/O where the system is smart enough to detect the truncation while data
is in the output buffers. Beware that after the temp file is released and
truncated, any operations on that file may fail miserably until it is
re-acquired. All contents are lost between each release and acquire mapped to
the same string.

=cut

sub temp_release {
	my ($self, $temp_fd, $trunc) = _maybe_self(@_);

	if (exists $TEMP_FILEMAP{$temp_fd}) {
		$temp_fd = $TEMP_FILES{$temp_fd};
	}
	unless ($TEMP_FILES{$temp_fd}{locked}) {
		carp "Attempt to release temp file '",
			$temp_fd, "' that has not been locked";
	}
	temp_reset($temp_fd) if $trunc and $temp_fd->opened;

	$TEMP_FILES{$temp_fd}{locked} = 0;
	undef;
}

sub _temp_cache {
	my ($self, $name) = _maybe_self(@_);

	my $temp_fd = \$TEMP_FILEMAP{$name};
	if (defined $$temp_fd and $$temp_fd->opened) {
		if ($TEMP_FILES{$$temp_fd}{locked}) {
			throw Error::Simple("Temp file with moniker '" .
				$name . "' already in use");
		}
	} else {
		if (defined $$temp_fd) {
			# then we're here because of a closed handle.
			carp "Temp file '", $name,
				"' was closed. Opening replacement.";
		}
		my $fname;

		my $tmpdir;
		if (defined $self) {
			$tmpdir = $self->repo_path();
		}

		my $n = $name;
		$n =~ s/\W/_/g; # no strange chars

		require File::Temp;
		($$temp_fd, $fname) = File::Temp::tempfile(
			"Git_${n}_XXXXXX", UNLINK => 1, DIR => $tmpdir,
			) or throw Error::Simple("couldn't open new temp file");

		$$temp_fd->autoflush;
		binmode $$temp_fd;
		$TEMP_FILES{$$temp_fd}{fname} = $fname;
	}
	$$temp_fd;
}

=item temp_reset ( FILEHANDLE )

Truncates and resets the position of the C<FILEHANDLE>.

=cut

sub temp_reset {
	my ($self, $temp_fd) = _maybe_self(@_);

	truncate $temp_fd, 0
		or throw Error::Simple("couldn't truncate file");
	sysseek($temp_fd, 0, Fcntl::SEEK_SET()) and seek($temp_fd, 0, Fcntl::SEEK_SET())
		or throw Error::Simple("couldn't seek to beginning of file");
	sysseek($temp_fd, 0, Fcntl::SEEK_CUR()) == 0 and tell($temp_fd) == 0
		or throw Error::Simple("expected file position to be reset");
}

=item temp_path ( NAME )

=item temp_path ( FILEHANDLE )

Returns the filename associated with the given tempfile.

=cut

sub temp_path {
	my ($self, $temp_fd) = _maybe_self(@_);

	if (exists $TEMP_FILEMAP{$temp_fd}) {
		$temp_fd = $TEMP_FILEMAP{$temp_fd};
	}
	$TEMP_FILES{$temp_fd}{fname};
}

sub END {
	unlink values %TEMP_FILEMAP if %TEMP_FILEMAP;
}

} # %TEMP_* Lexical Context

=item prefix_lines ( PREFIX, STRING [, STRING... ])

Prefixes lines in C<STRING> with C<PREFIX>.

=cut

sub prefix_lines {
	my $prefix = shift;
	my $string = join("\n", @_);
	$string =~ s/^/$prefix/mg;
	return $string;
}

=item unquote_path ( PATH )

Unquote a quoted path containing c-escapes as returned by ls-files etc.
when not using -z or when parsing the output of diff -u.

=cut

{
	my %cquote_map = (
		"a" => chr(7),
		"b" => chr(8),
		"t" => chr(9),
		"n" => chr(10),
		"v" => chr(11),
		"f" => chr(12),
		"r" => chr(13),
		"\\" => "\\",
		"\042" => "\042",
	);

	sub unquote_path {
		local ($_) = @_;
		my ($retval, $remainder);
		if (!/^\042(.*)\042$/) {
			return $_;
		}
		($_, $retval) = ($1, "");
		while (/^([^\\]*)\\(.*)$/) {
			$remainder = $2;
			$retval .= $1;
			for ($remainder) {
				if (/^([0-3][0-7][0-7])(.*)$/) {
					$retval .= chr(oct($1));
					$_ = $2;
					last;
				}
				if (/^([\\\042abtnvfr])(.*)$/) {
					$retval .= $cquote_map{$1};
					$_ = $2;
					last;
				}
				# This is malformed
				throw Error::Simple("invalid quoted path $_[0]");
			}
			$_ = $remainder;
		}
		$retval .= $_;
		return $retval;
	}
}

=item get_comment_line_char ( )

Gets the core.commentchar configuration value.
The value falls-back to '#' if core.commentchar is set to 'auto'.

=cut

sub get_comment_line_char {
	my $comment_line_char = config("core.commentchar") || '#';
	$comment_line_char = '#' if ($comment_line_char eq 'auto');
	$comment_line_char = '#' if (length($comment_line_char) != 1);
	return $comment_line_char;
}

=item comment_lines ( STRING [, STRING... ])

Comments lines following core.commentchar configuration.

=cut

sub comment_lines {
	my $comment_line_char = get_comment_line_char;
	return prefix_lines("$comment_line_char ", @_);
}

=back

=head1 ERROR HANDLING

All functions are supposed to throw Perl exceptions in case of errors.
See the L<Error> module on how to catch those. Most exceptions are mere
L<Error::Simple> instances.

However, the C<command()>, C<command_oneline()> and C<command_noisy()>
functions suite can throw C<Git::Error::Command> exceptions as well: those are
thrown when the external command returns an error code and contain the error
code as well as access to the captured command's output. The exception class
provides the usual C<stringify> and C<value> (command's exit code) methods and
in addition also a C<cmd_output> method that returns either an array or a
string with the captured command output (depending on the original function
call context; C<command_noisy()> returns C<undef>) and $<cmdline> which
returns the command and its arguments (but without proper quoting).

Note that the C<command_*_pipe()> functions cannot throw this exception since
it has no idea whether the command failed or not. You will only find out
at the time you C<close> the pipe; if you want to have that automated,
use C<command_close_pipe()>, which can throw the exception.

=cut

{
	package Git::Error::Command;

	@Git::Error::Command::ISA = qw(Error);

	sub new {
		my $self = shift;
		my $cmdline = '' . shift;
		my $value = 0 + shift;
		my $outputref = shift;
		my(@args) = ();

		local $Error::Depth = $Error::Depth + 1;

		push(@args, '-cmdline', $cmdline);
		push(@args, '-value', $value);
		push(@args, '-outputref', $outputref);

		$self->SUPER::new(-text => 'command returned error', @args);
	}

	sub stringify {
		my $self = shift;
		my $text = $self->SUPER::stringify;
		$self->cmdline() . ': ' . $text . ': ' . $self->value() . "\n";
	}

	sub cmdline {
		my $self = shift;
		$self->{'-cmdline'};
	}

	sub cmd_output {
		my $self = shift;
		my $ref = $self->{'-outputref'};
		defined $ref or undef;
		if (ref $ref eq 'ARRAY') {
			return @$ref;
		} else { # SCALAR
			return $$ref;
		}
	}
}

=over 4

=item git_cmd_try { CODE } ERRMSG

This magical statement will automatically catch any C<Git::Error::Command>
exceptions thrown by C<CODE> and make your program die with C<ERRMSG>
on its lips; the message will have %s substituted for the command line
and %d for the exit status. This statement is useful mostly for producing
more user-friendly error messages.

In case of no exception caught the statement returns C<CODE>'s return value.

Note that this is the only auto-exported function.

=cut

sub git_cmd_try(&$) {
	my ($code, $errmsg) = @_;
	my @result;
	my $err;
	my $array = wantarray;
	try {
		if ($array) {
			@result = &$code;
		} else {
			$result[0] = &$code;
		}
	} catch Git::Error::Command with {
		my $E = shift;
		$err = $errmsg;
		$err =~ s/\%s/$E->cmdline()/ge;
		$err =~ s/\%d/$E->value()/ge;
		# We can't croak here since Error.pm would mangle
		# that to Error::Simple.
	};
	$err and croak $err;
	return $array ? @result : $result[0];
}


=back

=head1 COPYRIGHT

Copyright 2006 by Petr Baudis E<lt>pasky@suse.czE<gt>.

This module is free software; it may be used, copied, modified
and distributed under the terms of the GNU General Public Licence,
either version 2, or (at your option) any later version.

=cut


# Take raw method argument list and return ($obj, @args) in case
# the method was called upon an instance and (undef, @args) if
# it was called directly.
sub _maybe_self {
	UNIVERSAL::isa($_[0], 'Git') ? @_ : (undef, @_);
}

# Check if the command id is something reasonable.
sub _check_valid_cmd {
	my ($cmd) = @_;
	$cmd =~ /^[a-z0-9A-Z_-]+$/ or throw Error::Simple("bad command: $cmd");
}

# Common backend for the pipe creators.
sub _command_common_pipe {
	my $direction = shift;
	my ($self, @p) = _maybe_self(@_);
	my (%opts, $cmd, @args);
	if (ref $p[0]) {
		($cmd, @args) = @{shift @p};
		%opts = ref $p[0] ? %{$p[0]} : @p;
	} else {
		($cmd, @args) = @p;
	}
	_check_valid_cmd($cmd);

	my $fh;
	if ($^O eq 'MSWin32') {
		# ActiveState Perl
		#defined $opts{STDERR} and
		#	warn 'ignoring STDERR option - running w/ ActiveState';
		$direction eq '-|' or
			die 'input pipe for ActiveState not implemented';
		# the strange construction with *ACPIPE is just to
		# explain the tie below that we want to bind to
		# a handle class, not scalar. It is not known if
		# it is something specific to ActiveState Perl or
		# just a Perl quirk.
		tie (*ACPIPE, 'Git::activestate_pipe', $cmd, @args);
		$fh = *ACPIPE;

	} else {
		my $pid = open($fh, $direction);
		if (not defined $pid) {
			throw Error::Simple("open failed: $!");
		} elsif ($pid == 0) {
			if ($opts{STDERR}) {
				open (STDERR, '>&', $opts{STDERR})
					or die "dup failed: $!";
			} elsif (defined $opts{STDERR}) {
				open (STDERR, '>', '/dev/null')
					or die "opening /dev/null failed: $!";
			}
			_cmd_exec($self, $cmd, @args);
		}
	}
	return wantarray ? ($fh, join(' ', $cmd, @args)) : $fh;
}

# When already in the subprocess, set up the appropriate state
# for the given repository and execute the git command.
sub _cmd_exec {
	my ($self, @args) = @_;
	_setup_git_cmd_env($self);
	_execv_git_cmd(@args);
	die qq[exec "@args" failed: $!];
}

# set up the appropriate state for git command
sub _setup_git_cmd_env {
	my $self = shift;
	if ($self) {
		$self->repo_path() and $ENV{'GIT_DIR'} = $self->repo_path();
		$self->repo_path() and $self->wc_path()
			and $ENV{'GIT_WORK_TREE'} = $self->wc_path();
		$self->wc_path() and chdir($self->wc_path());
		$self->wc_subdir() and chdir($self->wc_subdir());
	}
}

# Execute the given Git command ($_[0]) with arguments ($_[1..])
# by searching for it at proper places.
sub _execv_git_cmd { exec('git', @_); }

sub _is_sig {
	my ($v, $n) = @_;

	# We are avoiding a "use POSIX qw(SIGPIPE SIGABRT)" in the hot
	# Git.pm codepath.
	require POSIX;
	no strict 'refs';
	$v == *{"POSIX::$n"}->();
}

# Close pipe to a subprocess.
sub _cmd_close {
	my $ctx = shift @_;
	foreach my $fh (@_) {
		if (close $fh) {
			# nop
		} elsif ($!) {
			# It's just close, no point in fatalities
			carp "error closing pipe: $!";
		} elsif ($? >> 8) {
			# The caller should pepper this.
			throw Git::Error::Command($ctx, $? >> 8);
		} elsif ($? & 127 && _is_sig($? & 127, "SIGPIPE")) {
			# we might e.g. closed a live stream; the command
			# dying of SIGPIPE would drive us here.
		} elsif ($? & 127 && _is_sig($? & 127, "SIGABRT")) {
			die sprintf('BUG?: got SIGABRT ($? = %d, $? & 127 = %d) when closing pipe',
				    $?, $? & 127);
		} elsif ($? & 127) {
			die sprintf('got signal ($? = %d, $? & 127 = %d) when closing pipe',
				    $?, $? & 127);
		}
	}
}


sub DESTROY {
	my ($self) = @_;
	$self->_close_hash_and_insert_object();
	$self->_close_cat_blob();
}


# Pipe implementation for ActiveState Perl.

package Git::activestate_pipe;

sub TIEHANDLE {
	my ($class, @params) = @_;
	# FIXME: This is probably horrible idea and the thing will explode
	# at the moment you give it arguments that require some quoting,
	# but I have no ActiveState clue... --pasky
	# Let's just hope ActiveState Perl does at least the quoting
	# correctly.
	my @data = qx{git @params};
	bless { i => 0, data => \@data }, $class;
}

sub READLINE {
	my $self = shift;
	if ($self->{i} >= scalar @{$self->{data}}) {
		return undef;
	}
	my $i = $self->{i};
	if (wantarray) {
		$self->{i} = $#{$self->{'data'}} + 1;
		return splice(@{$self->{'data'}}, $i);
	}
	$self->{i} = $i + 1;
	return $self->{'data'}->[ $i ];
}

sub CLOSE {
	my $self = shift;
	delete $self->{data};
	delete $self->{i};
}

sub EOF {
	my $self = shift;
	return ($self->{i} >= scalar @{$self->{data}});
}


1; # Famous last words
