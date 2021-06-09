package Git::IndexInfo;
use strict;
use warnings $ENV{GIT_PERL_FATAL_WARNINGS} ? qw(FATAL all) : ();
use Git qw/command_input_pipe command_close_pipe/;

sub new {
	my ($class) = @_;
	my $hash_algo = Git::config('extensions.objectformat') || 'sha1';
	my ($gui, $ctx) = command_input_pipe(qw/update-index -z --index-info/);
	bless { gui => $gui, ctx => $ctx, nr => 0, hash_algo => $hash_algo}, $class;
}

sub remove {
	my ($self, $path) = @_;
	my $length = $self->{hash_algo} eq 'sha256' ? 64 : 40;
	if (print { $self->{gui} } '0 ', 0 x $length, "\t", $path, "\0") {
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

1;
