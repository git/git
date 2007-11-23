=head1 NAME

Git - Perl interface to the Git version control system

=cut


package Git;

use strict;


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

=cut


require Exporter;

@ISA = qw(Exporter);

@EXPORT = qw(git_cmd_try);

# Methods which can be called as standalone functions as well:
@EXPORT_OK = qw(command command_oneline command_noisy
                command_output_pipe command_input_pipe command_close_pipe
                version exec_path hash_object git_cmd_try);


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
increate nonwithstanding).

=cut


use Carp qw(carp croak); # but croak is bad - throw instead
use Error qw(:try);
use Cwd qw(abs_path);

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

	if (not defined $opts{Repository} and not defined $opts{WorkingCopy}) {
		$opts{Directory} ||= '.';
	}

	if ($opts{Directory}) {
		-d $opts{Directory} or throw Error::Simple("Directory not found: $!");

		my $search = Git->repository(WorkingCopy => $opts{Directory});
		my $dir;
		try {
			$dir = $search->command_oneline(['rev-parse', '--git-dir'],
			                                STDERR => 0);
		} catch Git::Error::Command with {
			$dir = undef;
		};

		if ($dir) {
			$dir =~ m#^/# or $dir = $opts{Directory} . '/' . $dir;
			$opts{Repository} = $dir;

			# If --git-dir went ok, this shouldn't die either.
			my $prefix = $search->command_oneline('rev-parse', '--show-prefix');
			$dir = abs_path($opts{Directory}) . '/';
			if ($prefix) {
				if (substr($dir, -length($prefix)) ne $prefix) {
					throw Error::Simple("rev-parse confused me - $dir does not have trailing $prefix");
				}
				substr($dir, -length($prefix)) = '';
			}
			$opts{WorkingCopy} = $dir;
			$opts{WorkingSubdir} = $prefix;

		} else {
			# A bare repository? Let's see...
			$dir = $opts{Directory};

			unless (-d "$dir/refs" and -d "$dir/objects" and -e "$dir/HEAD") {
				# Mimick git-rev-parse --git-dir error message:
				throw Error::Simple('fatal: Not a git repository');
			}
			my $search = Git->repository(Repository => $dir);
			try {
				$search->command('symbolic-ref', 'HEAD');
			} catch Git::Error::Command with {
				# Mimick git-rev-parse --git-dir error message:
				throw Error::Simple('fatal: Not a git repository');
			}

			$opts{Repository} = abs_path($dir);
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
		_cmd_close($fh, $ctx);

	} elsif (not wantarray) {
		local $/;
		my $text = <$fh>;
		try {
			_cmd_close($fh, $ctx);
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
			_cmd_close($fh, $ctx);
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
		_cmd_close($fh, $ctx);
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
	_cmd_close($fh, $ctx);
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
		or throw Error::Simple("subdir not found: $!");
	# Of course we will not "hold" the subdirectory so anyone
	# can delete it now and we will never know. But at least we tried.

	$self->{opts}->{WorkingSubdir} = $subdir;
}


=item config ( VARIABLE )

Retrieve the configuration C<VARIABLE> in the same manner as C<config>
does. In scalar context requires the variable to be set only one time
(exception is thrown otherwise), in array context returns allows the
variable to be set multiple times and returns all the values.

Must be called on a repository instance.

This currently wraps command('config') so it is not so fast.

=cut

sub config {
	my ($self, $var) = @_;
	$self->repo_path()
		or throw Error::Simple("not a repository");

	try {
		if (wantarray) {
			return $self->command('config', '--get-all', $var);
		} else {
			return $self->command_oneline('config', '--get', $var);
		}
	} catch Git::Error::Command with {
		my $E = shift;
		if ($E->value() == 1) {
			# Key not found.
			return undef;
		} else {
			throw $E;
		}
	};
}


=item config_bool ( VARIABLE )

Retrieve the bool configuration C<VARIABLE>. The return value
is usable as a boolean in perl (and C<undef> if it's not defined,
of course).

Must be called on a repository instance.

This currently wraps command('config') so it is not so fast.

=cut

sub config_bool {
	my ($self, $var) = @_;
	$self->repo_path()
		or throw Error::Simple("not a repository");

	try {
		my $val = $self->command_oneline('config', '--bool', '--get',
					      $var);
		return undef unless defined $val;
		return $val eq 'true';
	} catch Git::Error::Command with {
		my $E = shift;
		if ($E->value() == 1) {
			# Key not found.
			return undef;
		} else {
			throw $E;
		}
	};
}

=item config_int ( VARIABLE )

Retrieve the integer configuration C<VARIABLE>. The return value
is simple decimal number.  An optional value suffix of 'k', 'm',
or 'g' in the config file will cause the value to be multiplied
by 1024, 1048576 (1024^2), or 1073741824 (1024^3) prior to output.
It would return C<undef> if configuration variable is not defined,

Must be called on a repository instance.

This currently wraps command('config') so it is not so fast.

=cut

sub config_int {
	my ($self, $var) = @_;
	$self->repo_path()
		or throw Error::Simple("not a repository");

	try {
		return $self->command_oneline('config', '--int', '--get', $var);
	} catch Git::Error::Command with {
		my $E = shift;
		if ($E->value() == 1) {
			# Key not found.
			return undef;
		} else {
			throw $E;
		}
	};
}

=item ident ( TYPE | IDENTSTR )

=item ident_person ( TYPE | IDENTSTR | IDENTARRAY )

This suite of functions retrieves and parses ident information, as stored
in the commit and tag objects or produced by C<var GIT_type_IDENT> (thus
C<TYPE> can be either I<author> or I<committer>; case is insignificant).

The C<ident> method retrieves the ident information from C<git-var>
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

Both methods must be called on a repository instance.

=cut

sub ident {
	my ($self, $type) = @_;
	my $identstr;
	if (lc $type eq lc 'committer' or lc $type eq lc 'author') {
		$identstr = $self->command_oneline('var', 'GIT_'.uc($type).'_IDENT');
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
	my ($self, @ident) = @_;
	$#ident == 0 and @ident = $self->ident($ident[0]);
	return "$ident[0] <$ident[1]>";
}


=item hash_object ( TYPE, FILENAME )

Compute the SHA1 object id of the given C<FILENAME> (or data waiting in
C<FILEHANDLE>) considering it is of the C<TYPE> object type (C<blob>,
C<commit>, C<tree>).

The method can be called without any instance or on a specified Git repository,
it makes zero difference.

The function returns the SHA1 hash.

=cut

# TODO: Support for passing FILEHANDLE instead of FILENAME
sub hash_object {
	my ($self, $type, $file) = _maybe_self(@_);
	command_oneline('hash-object', '-t', $type, $file);
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
	# This breaks inheritance. Oh well.
	ref $_[0] eq 'Git' ? @_ : (undef, @_);
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
			if (defined $opts{STDERR}) {
				close STDERR;
			}
			if ($opts{STDERR}) {
				open (STDERR, '>&', $opts{STDERR})
					or die "dup failed: $!";
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
	if ($self) {
		$self->repo_path() and $ENV{'GIT_DIR'} = $self->repo_path();
		$self->wc_path() and chdir($self->wc_path());
		$self->wc_subdir() and chdir($self->wc_subdir());
	}
	_execv_git_cmd(@args);
	die qq[exec "@args" failed: $!];
}

# Execute the given Git command ($_[0]) with arguments ($_[1..])
# by searching for it at proper places.
sub _execv_git_cmd { exec('git', @_); }

# Close pipe to a subprocess.
sub _cmd_close {
	my ($fh, $ctx) = @_;
	if (not close $fh) {
		if ($!) {
			# It's just close, no point in fatalities
			carp "error closing pipe: $!";
		} elsif ($? >> 8) {
			# The caller should pepper this.
			throw Git::Error::Command($ctx, $? >> 8);
		}
		# else we might e.g. closed a live stream; the command
		# dying of SIGPIPE would drive us here.
	}
}


sub DESTROY { }


# Pipe implementation for ActiveState Perl.

package Git::activestate_pipe;
use strict;

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
