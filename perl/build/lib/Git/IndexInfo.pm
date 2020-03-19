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

1;
